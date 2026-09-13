#include "PortalScreenCast.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <random>
#include <string>

#include <unistd.h>

#include <dbus/dbus.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/utils/result.h>

#include <opencv2/imgproc.hpp>
#include <opencv2/core.hpp>

#include "core/Logger.h"

namespace
{
constexpr const char* kPortalService = "org.freedesktop.portal.Desktop";
constexpr const char* kPortalPath = "/org/freedesktop/portal/desktop";
constexpr const char* kScreenCastInterface = "org.freedesktop.portal.ScreenCast";
constexpr const char* kRequestInterface = "org.freedesktop.portal.Request";

std::string RandomToken()
{
    static std::mt19937 engine(std::random_device{}());
    std::uniform_int_distribution<int> distribution(0, 0x7fffffff);

    return "runehelper" + std::to_string(distribution(engine));
}

std::string SenderPart(DBusConnection* connection)
{
    const char* unique = dbus_bus_get_unique_name(connection);

    if (!unique)
        return {};

    std::string sender(unique[0] == ':' ? unique + 1 : unique);

    for (char& ch : sender)
    {
        if (ch == '.')
            ch = '_';
    }

    return sender;
}

void AppendVariantString(DBusMessageIter* dict, const char* key, const char* value)
{
    DBusMessageIter entry;
    DBusMessageIter variant;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, DBUS_TYPE_STRING_AS_STRING, &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

void AppendVariantUint(DBusMessageIter* dict, const char* key, dbus_uint32_t value)
{
    DBusMessageIter entry;
    DBusMessageIter variant;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, DBUS_TYPE_UINT32_AS_STRING, &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &value);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

void AppendVariantBool(DBusMessageIter* dict, const char* key, dbus_bool_t value)
{
    DBusMessageIter entry;
    DBusMessageIter variant;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, DBUS_TYPE_BOOLEAN_AS_STRING, &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &value);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

struct PortalResponse
{
    bool received = false;
    dbus_uint32_t code = 1;
    std::string sessionHandle;
    std::string restoreToken;
    dbus_uint32_t nodeId = 0;
    bool hasNode = false;
    cv::Point position{0, 0};
    bool hasPosition = false;
};

void ParseStreamProperties(DBusMessageIter* props, PortalResponse& response)
{
    DBusMessageIter dict;

    for (dbus_message_iter_recurse(props, &dict);
         dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY;
         dbus_message_iter_next(&dict))
    {
        DBusMessageIter entry;
        dbus_message_iter_recurse(&dict, &entry);

        const char* key = nullptr;
        dbus_message_iter_get_basic(&entry, &key);
        dbus_message_iter_next(&entry);

        if (!key || std::strcmp(key, "position") != 0)
            continue;

        DBusMessageIter value;
        dbus_message_iter_recurse(&entry, &value);

        if (dbus_message_iter_get_arg_type(&value) != DBUS_TYPE_STRUCT)
            continue;

        DBusMessageIter point;
        dbus_message_iter_recurse(&value, &point);

        dbus_int32_t x = 0;
        dbus_int32_t y = 0;

        dbus_message_iter_get_basic(&point, &x);
        dbus_message_iter_next(&point);
        dbus_message_iter_get_basic(&point, &y);

        response.position = cv::Point(static_cast<int>(x), static_cast<int>(y));
        response.hasPosition = true;
    }
}

void ParseResults(DBusMessageIter* results, PortalResponse& response)
{
    DBusMessageIter dict;

    for (dbus_message_iter_recurse(results, &dict);
         dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY;
         dbus_message_iter_next(&dict))
    {
        DBusMessageIter entry;
        dbus_message_iter_recurse(&dict, &entry);

        const char* key = nullptr;
        dbus_message_iter_get_basic(&entry, &key);
        dbus_message_iter_next(&entry);

        DBusMessageIter value;
        dbus_message_iter_recurse(&entry, &value);

        if (!key)
            continue;

        if (std::strcmp(key, "session_handle") == 0 && dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_STRING)
        {
            const char* handle = nullptr;
            dbus_message_iter_get_basic(&value, &handle);

            if (handle)
                response.sessionHandle = handle;
        }
        else if (std::strcmp(key, "restore_token") == 0 && dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_STRING)
        {
            const char* token = nullptr;
            dbus_message_iter_get_basic(&value, &token);

            if (token)
                response.restoreToken = token;
        }
        else if (std::strcmp(key, "streams") == 0 && dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_ARRAY)
        {
            DBusMessageIter streams;
            dbus_message_iter_recurse(&value, &streams);

            if (dbus_message_iter_get_arg_type(&streams) == DBUS_TYPE_STRUCT)
            {
                DBusMessageIter stream;
                dbus_message_iter_recurse(&streams, &stream);
                dbus_message_iter_get_basic(&stream, &response.nodeId);
                response.hasNode = true;

                dbus_message_iter_next(&stream);

                if (dbus_message_iter_get_arg_type(&stream) == DBUS_TYPE_ARRAY)
                    ParseStreamProperties(&stream, response);
            }
        }
    }
}

DBusHandlerResult HandleResponseSignal(DBusConnection*, DBusMessage* message, void* data)
{
    if (!dbus_message_is_signal(message, kRequestInterface, "Response"))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    auto* response = static_cast<PortalResponse*>(data);

    DBusMessageIter iter;

    if (!dbus_message_iter_init(message, &iter))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    dbus_message_iter_get_basic(&iter, &response->code);
    dbus_message_iter_next(&iter);

    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY)
        ParseResults(&iter, *response);

    response->received = true;
    return DBUS_HANDLER_RESULT_HANDLED;
}

bool WaitForResponse(DBusConnection* connection, PortalResponse& response, int timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    while (!response.received)
    {
        if (!dbus_connection_read_write_dispatch(connection, 100))
            return false;

        if (std::chrono::steady_clock::now() > deadline)
        {
            LOG_ERROR("Portal screencast: timed out waiting for a portal response");
            return false;
        }
    }

    return response.code == 0;
}
}

struct PortalScreenCast::Impl
{
    DBusConnection* connection = nullptr;
    std::string sessionHandle;

    pw_thread_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    pw_stream* stream = nullptr;
    spa_hook streamListener{};

    int pipewireFd = -1;
    std::atomic<bool> running{false};

    std::mutex frameMutex;
    cv::Mat frame;
    cv::Point position{0, 0};
    bool hasPosition = false;
    spa_video_info format{};

    void OnParamChanged(std::uint32_t id, const spa_pod* param);
    void OnProcess();

    static void ParamChanged(void* data, std::uint32_t id, const spa_pod* param);
    static void Process(void* data);
    static void StateChanged(void* data, pw_stream_state old, pw_stream_state state, const char* error);

    static const pw_stream_events kEvents;
};

void PortalScreenCast::Impl::ParamChanged(void* data, std::uint32_t id, const spa_pod* param)
{
    static_cast<Impl*>(data)->OnParamChanged(id, param);
}

void PortalScreenCast::Impl::Process(void* data)
{
    static_cast<Impl*>(data)->OnProcess();
}

void PortalScreenCast::Impl::StateChanged(void*, pw_stream_state old, pw_stream_state state, const char* error)
{
    LOG_INFO(
        std::string("Portal screencast: stream state ") + pw_stream_state_as_string(old) + " -> " +
        pw_stream_state_as_string(state) + (error ? std::string(" (") + error + ")" : std::string())
    );
}

const pw_stream_events PortalScreenCast::Impl::kEvents = []
{
    pw_stream_events events{};
    events.version = PW_VERSION_STREAM_EVENTS;
    events.state_changed = &PortalScreenCast::Impl::StateChanged;
    events.param_changed = &PortalScreenCast::Impl::ParamChanged;
    events.process = &PortalScreenCast::Impl::Process;
    return events;
}();

void PortalScreenCast::Impl::OnParamChanged(std::uint32_t id, const spa_pod* param)
{
    if (!param || id != SPA_PARAM_Format)
        return;

    spa_video_info info{};

    if (spa_format_parse(param, &info.media_type, &info.media_subtype) < 0)
        return;

    if (info.media_type != SPA_MEDIA_TYPE_video || info.media_subtype != SPA_MEDIA_SUBTYPE_raw)
        return;

    if (spa_format_video_raw_parse(param, &info.info.raw) < 0)
        return;

    format = info;

    LOG_INFO(
        "Portal screencast: negotiated " + std::to_string(info.info.raw.size.width) + "x" +
        std::to_string(info.info.raw.size.height) + " format " + std::to_string(static_cast<int>(info.info.raw.format))
    );
}

void PortalScreenCast::Impl::OnProcess()
{
    pw_buffer* buffer = pw_stream_dequeue_buffer(stream);

    if (!buffer)
        return;

    spa_buffer* spaBuffer = buffer->buffer;

    if (spaBuffer->n_datas > 0 && spaBuffer->datas[0].data)
    {
        const spa_data& data = spaBuffer->datas[0];
        const int width = static_cast<int>(format.info.raw.size.width);
        const int height = static_cast<int>(format.info.raw.size.height);
        const int stride = data.chunk->stride > 0 ? data.chunk->stride : width * 4;

        if (width > 0 && height > 0)
        {
            cv::Mat wrapped(height, width, CV_8UC4, data.data, static_cast<std::size_t>(stride));
            cv::Mat converted;

            switch (format.info.raw.format)
            {
            case SPA_VIDEO_FORMAT_BGRx:
            case SPA_VIDEO_FORMAT_BGRA:
                cv::cvtColor(wrapped, converted, cv::COLOR_BGRA2BGR);
                break;
            case SPA_VIDEO_FORMAT_RGBx:
            case SPA_VIDEO_FORMAT_RGBA:
                cv::cvtColor(wrapped, converted, cv::COLOR_RGBA2BGR);
                break;
            default:
                converted = cv::Mat();
                break;
            }

            if (!converted.empty())
            {
                std::lock_guard lock(frameMutex);
                frame = converted;
            }
        }
    }

    pw_stream_queue_buffer(stream, buffer);
}

PortalScreenCast::PortalScreenCast()
    : impl_(new Impl())
{
}

PortalScreenCast::~PortalScreenCast()
{
    Stop();
    delete impl_;
    impl_ = nullptr;
}

bool PortalScreenCast::IsRunning() const
{
    return impl_ && impl_->running.load();
}

cv::Mat PortalScreenCast::LatestFrame()
{
    if (!impl_)
        return {};

    std::lock_guard lock(impl_->frameMutex);
    return impl_->frame;
}

cv::Point PortalScreenCast::FramePosition() const
{
    return impl_ ? impl_->position : cv::Point(0, 0);
}

bool PortalScreenCast::HasFramePosition() const
{
    return impl_ && impl_->hasPosition;
}

namespace
{
bool CallAndWait(
    DBusConnection* connection,
    DBusMessage* message,
    const std::string& requestPath,
    PortalResponse& response,
    int timeoutMs
)
{
    const std::string match =
        "type='signal',interface='" + std::string(kRequestInterface) + "',path='" + requestPath + "'";

    DBusError error;
    dbus_error_init(&error);

    dbus_bus_add_match(connection, match.c_str(), &error);
    dbus_connection_flush(connection);

    if (dbus_error_is_set(&error))
    {
        LOG_ERROR(std::string("Portal screencast: add_match failed: ") + error.message);
        dbus_error_free(&error);
        dbus_message_unref(message);
        return false;
    }

    dbus_connection_add_filter(connection, HandleResponseSignal, &response, nullptr);

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, message, timeoutMs, &error);
    dbus_message_unref(message);

    bool ok = false;

    if (!reply)
    {
        LOG_ERROR(std::string("Portal screencast: call failed: ") + (error.message ? error.message : "unknown"));
        dbus_error_free(&error);
    }
    else
    {
        dbus_message_unref(reply);
        ok = WaitForResponse(connection, response, timeoutMs);
    }

    dbus_connection_remove_filter(connection, HandleResponseSignal, &response);
    dbus_bus_remove_match(connection, match.c_str(), nullptr);
    return ok;
}

DBusMessage* NewScreenCastCall(const char* method)
{
    return dbus_message_new_method_call(kPortalService, kPortalPath, kScreenCastInterface, method);
}
}

bool PortalScreenCast::Start(std::string& restoreToken)
{
    if (impl_->running.load())
        return true;

    DBusError error;
    dbus_error_init(&error);

    impl_->connection = dbus_bus_get(DBUS_BUS_SESSION, &error);

    if (!impl_->connection)
    {
        LOG_ERROR(std::string("Portal screencast: no session bus: ") + (error.message ? error.message : "unknown"));
        dbus_error_free(&error);
        return false;
    }

    dbus_connection_set_exit_on_disconnect(impl_->connection, FALSE);

    const std::string sender = SenderPart(impl_->connection);

    {
        const std::string handleToken = RandomToken();
        const std::string sessionToken = RandomToken();
        const std::string requestPath = "/org/freedesktop/portal/desktop/request/" + sender + "/" + handleToken;

        DBusMessage* message = NewScreenCastCall("CreateSession");
        DBusMessageIter args;
        DBusMessageIter options;

        dbus_message_iter_init_append(message, &args);
        dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &options);
        AppendVariantString(&options, "handle_token", handleToken.c_str());
        AppendVariantString(&options, "session_handle_token", sessionToken.c_str());
        dbus_message_iter_close_container(&args, &options);

        PortalResponse response;

        if (!CallAndWait(impl_->connection, message, requestPath, response, 30000))
        {
            LOG_ERROR("Portal screencast: CreateSession failed");
            return false;
        }

        impl_->sessionHandle = response.sessionHandle;
    }

    if (impl_->sessionHandle.empty())
    {
        LOG_ERROR("Portal screencast: portal returned no session handle");
        return false;
    }

    {
        const std::string handleToken = RandomToken();
        const std::string requestPath = "/org/freedesktop/portal/desktop/request/" + sender + "/" + handleToken;

        DBusMessage* message = NewScreenCastCall("SelectSources");
        DBusMessageIter args;
        DBusMessageIter options;

        const char* session = impl_->sessionHandle.c_str();
        dbus_message_iter_init_append(message, &args);
        dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH, &session);
        dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &options);
        AppendVariantString(&options, "handle_token", handleToken.c_str());
        AppendVariantUint(&options, "types", 1);
        AppendVariantBool(&options, "multiple", FALSE);
        AppendVariantUint(&options, "cursor_mode", 1);
        AppendVariantUint(&options, "persist_mode", 2);

        if (!restoreToken.empty())
            AppendVariantString(&options, "restore_token", restoreToken.c_str());

        dbus_message_iter_close_container(&args, &options);

        PortalResponse response;

        if (!CallAndWait(impl_->connection, message, requestPath, response, 120000))
        {
            LOG_ERROR("Portal screencast: SelectSources failed");
            return false;
        }
    }

    dbus_uint32_t nodeId = 0;

    {
        const std::string handleToken = RandomToken();
        const std::string requestPath = "/org/freedesktop/portal/desktop/request/" + sender + "/" + handleToken;

        DBusMessage* message = NewScreenCastCall("Start");
        DBusMessageIter args;
        DBusMessageIter options;

        const char* session = impl_->sessionHandle.c_str();
        const char* parent = "";

        dbus_message_iter_init_append(message, &args);
        dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH, &session);
        dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &parent);
        dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &options);
        AppendVariantString(&options, "handle_token", handleToken.c_str());
        dbus_message_iter_close_container(&args, &options);

        PortalResponse response;

        if (!CallAndWait(impl_->connection, message, requestPath, response, 300000))
        {
            LOG_ERROR("Portal screencast: Start failed or was denied");
            return false;
        }

        if (!response.hasNode)
        {
            LOG_ERROR("Portal screencast: portal returned no stream");
            return false;
        }

        nodeId = response.nodeId;

        if (response.hasPosition)
        {
            impl_->position = response.position;
            impl_->hasPosition = true;
        }

        if (!response.restoreToken.empty())
            restoreToken = response.restoreToken;
    }

    {
        DBusMessage* message = NewScreenCastCall("OpenPipeWireRemote");
        DBusMessageIter args;
        DBusMessageIter options;

        const char* session = impl_->sessionHandle.c_str();
        dbus_message_iter_init_append(message, &args);
        dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH, &session);
        dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &options);
        dbus_message_iter_close_container(&args, &options);

        DBusMessage* reply = dbus_connection_send_with_reply_and_block(impl_->connection, message, 30000, &error);
        dbus_message_unref(message);

        if (!reply)
        {
            LOG_ERROR(std::string("Portal screencast: OpenPipeWireRemote failed: ") + (error.message ? error.message : "unknown"));
            dbus_error_free(&error);
            return false;
        }

        if (!dbus_message_get_args(reply, &error, DBUS_TYPE_UNIX_FD, &impl_->pipewireFd, DBUS_TYPE_INVALID))
        {
            LOG_ERROR("Portal screencast: portal returned no pipewire fd");
            dbus_message_unref(reply);
            return false;
        }

        dbus_message_unref(reply);
    }

    pw_init(nullptr, nullptr);

    impl_->loop = pw_thread_loop_new("runehelper-capture", nullptr);

    if (!impl_->loop)
    {
        LOG_ERROR("Portal screencast: pw_thread_loop_new failed");
        return false;
    }

    pw_thread_loop_lock(impl_->loop);

    impl_->context = pw_context_new(pw_thread_loop_get_loop(impl_->loop), nullptr, 0);
    impl_->core = impl_->context ? pw_context_connect_fd(impl_->context, impl_->pipewireFd, nullptr, 0) : nullptr;

    if (!impl_->core)
    {
        LOG_ERROR("Portal screencast: could not connect to the pipewire remote");
        pw_thread_loop_unlock(impl_->loop);
        Stop();
        return false;
    }

    impl_->stream = pw_stream_new(
        impl_->core,
        "runehelper-capture",
        pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Video",
            PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_MEDIA_ROLE, "Screen",
            nullptr
        )
    );

    if (!impl_->stream)
    {
        LOG_ERROR("Portal screencast: pw_stream_new failed");
        pw_thread_loop_unlock(impl_->loop);
        Stop();
        return false;
    }

    pw_stream_add_listener(impl_->stream, &impl_->streamListener, &Impl::kEvents, impl_);

    std::uint8_t podBuffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(podBuffer, sizeof(podBuffer));

    spa_rectangle sizeDefault{1920, 1080};
    spa_rectangle sizeMin{1, 1};
    spa_rectangle sizeMax{8192, 8192};
    spa_fraction rateDefault{10, 1};
    spa_fraction rateMin{0, 1};
    spa_fraction rateMax{15, 1};

    const spa_pod* params[1];
    params[0] = static_cast<const spa_pod*>(spa_pod_builder_add_object(
        &builder,
        SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format,
        SPA_POD_CHOICE_ENUM_Id(
            5,
            SPA_VIDEO_FORMAT_BGRx,
            SPA_VIDEO_FORMAT_BGRx,
            SPA_VIDEO_FORMAT_RGBx,
            SPA_VIDEO_FORMAT_BGRA,
            SPA_VIDEO_FORMAT_RGBA
        ),
        SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&sizeDefault, &sizeMin, &sizeMax),
        SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(&rateDefault, &rateMin, &rateMax)
    ));

    const int connectResult = pw_stream_connect(
        impl_->stream,
        PW_DIRECTION_INPUT,
        nodeId,
        static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
        params,
        1
    );

    pw_thread_loop_unlock(impl_->loop);

    if (connectResult < 0)
    {
        LOG_ERROR("Portal screencast: pw_stream_connect failed: " + std::string(spa_strerror(connectResult)));
        Stop();
        return false;
    }

    if (pw_thread_loop_start(impl_->loop) < 0)
    {
        LOG_ERROR("Portal screencast: pw_thread_loop_start failed");
        Stop();
        return false;
    }

    impl_->running = true;

    LOG_INFO(
        "Portal screencast: capture stream started on node " + std::to_string(nodeId) +
        (impl_->hasPosition
            ? " at " + std::to_string(impl_->position.x) + "," + std::to_string(impl_->position.y)
            : " without a reported position")
    );
    return true;
}

void PortalScreenCast::Stop()
{
    if (!impl_)
        return;

    impl_->running = false;

    if (impl_->loop)
        pw_thread_loop_stop(impl_->loop);

    if (impl_->stream)
    {
        pw_stream_destroy(impl_->stream);
        impl_->stream = nullptr;
    }

    if (impl_->core)
    {
        pw_core_disconnect(impl_->core);
        impl_->core = nullptr;
    }

    if (impl_->context)
    {
        pw_context_destroy(impl_->context);
        impl_->context = nullptr;
    }

    if (impl_->loop)
    {
        pw_thread_loop_destroy(impl_->loop);
        impl_->loop = nullptr;
    }

    if (impl_->pipewireFd >= 0)
    {
        close(impl_->pipewireFd);
        impl_->pipewireFd = -1;
    }

    if (impl_->connection)
    {
        if (!impl_->sessionHandle.empty())
        {
            DBusMessage* message = dbus_message_new_method_call(
                kPortalService,
                impl_->sessionHandle.c_str(),
                "org.freedesktop.portal.Session",
                "Close"
            );

            if (message)
            {
                dbus_connection_send(impl_->connection, message, nullptr);
                dbus_connection_flush(impl_->connection);
                dbus_message_unref(message);
            }

            impl_->sessionHandle.clear();
        }

        dbus_connection_unref(impl_->connection);
        impl_->connection = nullptr;
    }
}
