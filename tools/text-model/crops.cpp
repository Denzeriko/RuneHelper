#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "TestScenes.h"
#include "ocr/OCR.h"
#include "platform/PlatformPaths.h"

namespace fs = std::filesystem;

namespace
{
struct Scene
{
    const char* name = "";
    double gain = 1.0;
    double screenWidth = 0.0;
    Surroundings surroundings = Surroundings::None;
    double margin = 1.0;
};

std::string ReadFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

cv::Mat Render(const fs::path& file, double panelWidth, const Scene& scene)
{
    cv::Mat gray;
    cv::cvtColor(cv::imread(file.string(), cv::IMREAD_COLOR), gray, cv::COLOR_BGR2GRAY);
    gray.convertTo(gray, -1, scene.gain, 0.0);
    gray = SurroundWithGame(gray, scene.surroundings, scene.margin);

    if (scene.screenWidth > 0.0 && scene.screenWidth != panelWidth)
    {
        const double factor = scene.screenWidth / panelWidth;
        cv::resize(gray, gray, cv::Size(), factor, factor, cv::INTER_CUBIC);
    }

    return gray;
}

int CopyRows(const fs::path& debugDir, const fs::path& target, const std::string& stem)
{
    int rows = 0;

    for (;; ++rows)
    {
        char name[32];
        std::snprintf(name, sizeof(name), "row_%02d_text.png", rows);

        if (!fs::exists(debugDir / name))
            break;

        char base[128];
        std::snprintf(base, sizeof(base), "%s_row%02d", stem.c_str(), rows);
        fs::copy_file(debugDir / name, target / (std::string(base) + ".png"), fs::copy_options::overwrite_existing);

        std::snprintf(name, sizeof(name), "row_%02d_read.txt", rows);

        if (fs::exists(debugDir / name))
            fs::copy_file(debugDir / name, target / (std::string(base) + ".read.txt"), fs::copy_options::overwrite_existing);
    }

    return rows;
}
}

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::printf("usage: text_model_crops <model> <panels> <out>\n");
        return 2;
    }

    const fs::path panelsDir = argv[2];
    const fs::path out = argv[3];

    cv::setNumThreads(1);

    OCR ocr;

    if (!ocr.Init(ReadFile(argv[1])))
    {
        std::printf("text_model_crops: %s is not a text model\n", argv[1]);
        return 2;
    }

    std::vector<fs::path> files;

    for (const auto& entry : fs::recursive_directory_iterator(panelsDir))
    {
        if (entry.path().extension() == ".png")
            files.push_back(entry.path());
    }

    std::sort(files.begin(), files.end());

    const std::vector<Scene> scenes = {
        { .name = "clean" },
        { .name = "dim", .gain = 0.7 },
        { .name = "uhd", .screenWidth = 3840.0 },
        { .name = "busy", .surroundings = Surroundings::Busy },
        { .name = "narrow", .surroundings = Surroundings::Busy, .margin = 0.3 },
    };

    const fs::path debugDir = GetUserDataDir() / "ocr_debug" / "latest";

    for (const Scene& scene : scenes)
    {
        const fs::path target = out / scene.name;
        fs::remove_all(target);
        fs::create_directories(target);

        int crops = 0;

        for (const fs::path& file : files)
        {
            const fs::path relative = fs::relative(file, panelsDir);
            const std::string resolution = relative.begin()->string();

            ocr.RecognizeLoot(Render(file, std::stod(resolution), scene), nullptr, true);
            crops += CopyRows(debugDir, target, resolution + "_" + file.stem().string());
        }

        std::printf("%-6s %d crops\n", scene.name, crops);
    }

    return 0;
}
