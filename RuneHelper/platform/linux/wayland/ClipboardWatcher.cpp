#include "platform/ClipboardWatcher.h"

#include <cerrno>
#include <chrono>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

#include "ext-data-control-v1-client-protocol.h"
#include "wlr-data-control-unstable-v1-client-protocol.h"
#include "platform/linux/wayland/WaylandSession.h"

struct ClipboardWatcher::Impl
{
    struct Offer
    {
        ext_data_control_offer_v1* ext = nullptr;
        zwlr_data_control_offer_v1* wlr = nullptr;
        std::string mime;

        ~Offer()
        {
            if (ext)
                ext_data_control_offer_v1_destroy(ext);
            if (wlr)
                zwlr_data_control_offer_v1_destroy(wlr);
        }

        void Receive(int fd) const
        {
            if (ext)
                ext_data_control_offer_v1_receive(ext, mime.c_str(), fd);
            else
                zwlr_data_control_offer_v1_receive(wlr, mime.c_str(), fd);
        }
    };

    WaylandSession session;
    wl_registry* registry = nullptr;
    ext_data_control_manager_v1* extManager = nullptr;
    zwlr_data_control_manager_v1* wlrManager = nullptr;
    ext_data_control_device_v1* extDevice = nullptr;
    zwlr_data_control_device_v1* wlrDevice = nullptr;
    std::unordered_map<void*, std::unique_ptr<Offer>> offers;
    bool initial = true;
    bool finished = false;
    int readFd = -1;
    std::string buffer;
    std::optional<std::string> ready;
    std::chrono::steady_clock::time_point deadline{};

    void CloseRead()
    {
        if (readFd >= 0)
            close(readFd);

        readFd = -1;
        buffer.clear();
    }

    static void Global(void* data, wl_registry* registry, std::uint32_t name, const char* interface, std::uint32_t)
    {
        auto& self = *static_cast<Impl*>(data);

        if (std::string_view(interface) == ext_data_control_manager_v1_interface.name && !self.extManager)
            self.extManager =
                static_cast<ext_data_control_manager_v1*>(wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, 1));
        else if (std::string_view(interface) == zwlr_data_control_manager_v1_interface.name && !self.wlrManager)
            self.wlrManager =
                static_cast<zwlr_data_control_manager_v1*>(wl_registry_bind(registry, name, &zwlr_data_control_manager_v1_interface, 1)
                );
    }

    template <typename T>
    static void Mime(void* data, T*, const char* mime)
    {
        auto& offer = *static_cast<Offer*>(data);
        const std::string_view type(mime);

        if (type == "text/plain;charset=utf-8" || ((type == "text/plain" || type == "UTF8_STRING") && offer.mime.empty()))
            offer.mime = type;
    }

    template <typename Device, typename Offered>
    static void DataOffer(void* data, Device*, Offered* object)
    {
        auto& self = *static_cast<Impl*>(data);
        auto offer = std::make_unique<Offer>();

        if constexpr (std::is_same_v<Offered, ext_data_control_offer_v1>)
        {
            offer->ext = object;
            static const ext_data_control_offer_v1_listener listener{ Mime<Offered> };
            ext_data_control_offer_v1_add_listener(object, &listener, offer.get());
        }
        else
        {
            offer->wlr = object;
            static const zwlr_data_control_offer_v1_listener listener{ Mime<Offered> };
            zwlr_data_control_offer_v1_add_listener(object, &listener, offer.get());
        }

        self.offers.emplace(object, std::move(offer));
    }

    template <typename Device, typename Offered>
    static void Selection(void* data, Device*, Offered* object)
    {
        auto& self = *static_cast<Impl*>(data);
        self.CloseRead();

        if (!self.initial)
        {
            self.ready = std::string{};
            const auto found = self.offers.find(object);

            if (found != self.offers.end() && !found->second->mime.empty())
            {
                int pipeFds[2];

                if (pipe2(pipeFds, O_CLOEXEC) == 0)
                {
                    if (fcntl(pipeFds[0], F_SETFL, O_NONBLOCK) != -1)
                    {
                        self.readFd = pipeFds[0];
                        self.ready.reset();
                        self.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                        found->second->Receive(pipeFds[1]);
                    }
                    else
                        close(pipeFds[0]);

                    close(pipeFds[1]);
                }
            }
        }

        self.offers.erase(object);
    }

    template <typename Device, typename Offered>
    static void PrimarySelection(void* data, Device*, Offered* object)
    {
        static_cast<Impl*>(data)->offers.erase(object);
    }

    template <typename Device>
    static void Finished(void* data, Device*)
    {
        static_cast<Impl*>(data)->finished = true;
    }

    void Read()
    {
        while (readFd >= 0)
        {
            char chunk[4096];
            const auto count = read(readFd, chunk, sizeof(chunk));

            if (count > 0)
            {
                buffer.append(chunk, static_cast<std::size_t>(count));

                if (buffer.size() <= kMaxClipboardText)
                    continue;
            }
            else if (count == 0)
            {
                ready = std::move(buffer);
                CloseRead();
                return;
            }
            else if ((errno == EAGAIN || errno == EINTR) && std::chrono::steady_clock::now() < deadline)
                return;

            ready = std::string{};
            CloseRead();
        }
    }
};

ClipboardWatcher::ClipboardWatcher() : impl_(std::make_unique<Impl>()) {}

ClipboardWatcher::~ClipboardWatcher()
{
    Stop();
}

bool ClipboardWatcher::Start()
{
    if (Running())
        return true;

    Stop();

    if (!impl_->session.Connect() || !impl_->session.Seat())
    {
        Stop();
        return false;
    }

    impl_->registry = wl_display_get_registry(impl_->session.Display());
    static const wl_registry_listener registryListener{ Impl::Global, [](void*, wl_registry*, std::uint32_t) {} };
    wl_registry_add_listener(impl_->registry, &registryListener, impl_.get());

    if (!impl_->session.Roundtrip())
    {
        Stop();
        return false;
    }

    if (impl_->extManager)
    {
        impl_->extDevice = ext_data_control_manager_v1_get_data_device(impl_->extManager, impl_->session.Seat());
        static const ext_data_control_device_v1_listener listener{ Impl::DataOffer,
                                                                   Impl::Selection,
                                                                   Impl::Finished,
                                                                   Impl::PrimarySelection };
        ext_data_control_device_v1_add_listener(impl_->extDevice, &listener, impl_.get());
    }
    else if (impl_->wlrManager)
    {
        impl_->wlrDevice = zwlr_data_control_manager_v1_get_data_device(impl_->wlrManager, impl_->session.Seat());
        static const zwlr_data_control_device_v1_listener listener{ Impl::DataOffer,
                                                                    Impl::Selection,
                                                                    Impl::Finished,
                                                                    Impl::PrimarySelection };
        zwlr_data_control_device_v1_add_listener(impl_->wlrDevice, &listener, impl_.get());
    }

    if (!Running() || !impl_->session.Roundtrip() || impl_->finished)
    {
        Stop();
        return false;
    }

    impl_->initial = false;
    return true;
}

void ClipboardWatcher::Stop()
{
    impl_->CloseRead();
    impl_->offers.clear();

    if (impl_->extDevice)
        ext_data_control_device_v1_destroy(impl_->extDevice);
    if (impl_->wlrDevice)
        zwlr_data_control_device_v1_destroy(impl_->wlrDevice);
    if (impl_->extManager)
        ext_data_control_manager_v1_destroy(impl_->extManager);
    if (impl_->wlrManager)
        zwlr_data_control_manager_v1_destroy(impl_->wlrManager);
    if (impl_->registry)
        wl_registry_destroy(impl_->registry);

    impl_->extDevice = nullptr;
    impl_->wlrDevice = nullptr;
    impl_->extManager = nullptr;
    impl_->wlrManager = nullptr;
    impl_->registry = nullptr;
    impl_->session.Disconnect();
    impl_->ready.reset();
    impl_->initial = true;
    impl_->finished = false;
}

bool ClipboardWatcher::Running() const
{
    return (impl_->extDevice || impl_->wlrDevice) && !impl_->finished;
}

std::optional<std::string> ClipboardWatcher::Poll()
{
    if (!Running())
        return std::nullopt;

    if (!impl_->session.DispatchFor(0) || impl_->finished)
    {
        Stop();
        return std::string{};
    }

    impl_->session.Flush();
    impl_->Read();
    return std::exchange(impl_->ready, std::nullopt);
}
