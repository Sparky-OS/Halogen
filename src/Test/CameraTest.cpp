// Copyright 2026 Daniel Campos Ramos
// SPDX-License-Identifier: MIT

#include <Corrade/TestSuite/Compare/Numeric.h>
#include <Corrade/TestSuite/Tester.h>
#include <Corrade/Utility/Debug.h>

#include <anari/anari.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct DeviceFixture {
    ANARILibrary library = nullptr;
    ANARIDevice device = nullptr;

    DeviceFixture() {
        auto statusFunc = [](const void *, ANARIDevice, ANARIObject,
            ANARIDataType, ANARIStatusSeverity severity, ANARIStatusCode,
            const char *message) {
            if (severity <= ANARI_SEVERITY_WARNING)
                Corrade::Utility::Debug{} << "ANARI:" << message;
        };
        library = anariLoadLibrary("halogen", statusFunc);
        if (library)
            device = anariNewDevice(library, "default");
        if (device)
            anariCommitParameters(device, device);
    }

    ~DeviceFixture() {
        if (device) anariRelease(device, device);
        if (library) anariUnloadLibrary(library);
    }
};

struct Image {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;
};

// A wide quad at z = -3 whose colour changes along x and y, so that every
// column of the picture is different and a shift of the frustum shows.
struct Scene {
    ANARIDevice device;
    ANARIWorld world;
    ANARIRenderer renderer;
    ANARIGeometry mesh;
    ANARIMaterial mat;
    ANARISurface surface;
    ANARILight light;
    ANARIArray1D arrays[5];

    explicit Scene(ANARIDevice d) : device(d)
    {
        static const float vertex[] = {
            -8.0f, -3.0f, -3.0f, 8.0f, -3.0f, -3.0f,
            -8.0f, 3.0f, -3.0f, 8.0f, 3.0f, -3.0f};
        static const float color[] = {
            0.0f, 0.1f, 0.2f, 1.0f, 1.0f, 0.1f, 0.2f, 1.0f,
            0.0f, 0.9f, 0.8f, 1.0f, 1.0f, 0.9f, 0.8f, 1.0f};
        static const uint32_t index[] = {0, 1, 2, 1, 3, 2};
        arrays[0] = anariNewArray1D(d, vertex, nullptr, nullptr,
            ANARI_FLOAT32_VEC3, 4);
        arrays[1] = anariNewArray1D(d, color, nullptr, nullptr,
            ANARI_FLOAT32_VEC4, 4);
        arrays[2] = anariNewArray1D(d, index, nullptr, nullptr,
            ANARI_UINT32_VEC3, 2);
        mesh = anariNewGeometry(d, "triangle");
        anariSetParameter(d, mesh, "vertex.position", ANARI_ARRAY1D,
            &arrays[0]);
        anariSetParameter(d, mesh, "vertex.color", ANARI_ARRAY1D, &arrays[1]);
        anariSetParameter(d, mesh, "primitive.index", ANARI_ARRAY1D,
            &arrays[2]);
        anariCommitParameters(d, mesh);
        mat = anariNewMaterial(d, "matte");
        anariSetParameter(d, mat, "color", ANARI_STRING, "color");
        anariCommitParameters(d, mat);
        surface = anariNewSurface(d);
        anariSetParameter(d, surface, "geometry", ANARI_GEOMETRY, &mesh);
        anariSetParameter(d, surface, "material", ANARI_MATERIAL, &mat);
        anariCommitParameters(d, surface);
        world = anariNewWorld(d);
        arrays[3] = anariNewArray1D(d, &surface, nullptr, nullptr,
            ANARI_SURFACE, 1);
        anariSetParameter(d, world, "surface", ANARI_ARRAY1D, &arrays[3]);
        light = anariNewLight(d, "directional");
        anariCommitParameters(d, light);
        arrays[4] = anariNewArray1D(d, &light, nullptr, nullptr,
            ANARI_LIGHT, 1);
        anariSetParameter(d, world, "light", ANARI_ARRAY1D, &arrays[4]);
        anariCommitParameters(d, world);
        renderer = anariNewRenderer(d, "default");
        anariCommitParameters(d, renderer);

        // The first frame of a world builds the engine's objects and is not
        // a reference for anything.
        render(8, 8, 1.0f, nullptr);
    }

    ~Scene()
    {
        anariRelease(device, renderer);
        anariRelease(device, world);
        anariRelease(device, surface);
        anariRelease(device, mat);
        anariRelease(device, mesh);
        anariRelease(device, light);
        for (ANARIArray1D a : arrays)
            anariRelease(device, a);
    }

    // aspect is the camera's; the frame is width x height, region may be null.
    Image render(uint32_t width, uint32_t height, float aspect,
        const float *region, const char *subtype = "perspective")
    {
        ANARICamera camera = anariNewCamera(device, subtype);
        anariSetParameter(device, camera, "aspect", ANARI_FLOAT32, &aspect);
        const float height1 = 4.0f;
        if (std::string(subtype) == "orthographic")
            anariSetParameter(device, camera, "height", ANARI_FLOAT32,
                &height1);
        if (region)
            anariSetParameter(device, camera, "imageRegion",
                ANARI_FLOAT32_BOX2, region);
        anariCommitParameters(device, camera);

        ANARIFrame frame = anariNewFrame(device);
        const uint32_t size[] = {width, height};
        const ANARIDataType colorType = ANARI_UFIXED8_RGBA_SRGB;
        anariSetParameter(device, frame, "size", ANARI_UINT32_VEC2, size);
        anariSetParameter(device, frame, "channel.color", ANARI_DATA_TYPE,
            &colorType);
        anariSetParameter(device, frame, "renderer", ANARI_RENDERER,
            &renderer);
        anariSetParameter(device, frame, "camera", ANARI_CAMERA, &camera);
        anariSetParameter(device, frame, "world", ANARI_WORLD, &world);
        anariCommitParameters(device, frame);
        anariRenderFrame(device, frame);
        anariFrameReady(device, frame, ANARI_WAIT);

        Image image;
        ANARIDataType type = ANARI_UNKNOWN;
        auto *fb = static_cast<const uint8_t *>(anariMapFrame(device, frame,
            "channel.color", &image.width, &image.height, &type));
        if (fb)
            image.rgba.assign(fb, fb + size_t(image.width) * image.height * 4);
        anariUnmapFrame(device, frame, "channel.color");
        anariRelease(device, frame);
        anariRelease(device, camera);
        return image;
    }
};

// The largest difference of any channel between a window of a and the
// whole of b; a's window starts at column ax.
int maxDifference(const Image &a, uint32_t ax, const Image &b, uint32_t bx,
    uint32_t columns)
{
    int worst = 0;
    for (uint32_t y = 0; y < b.height; ++y)
        for (uint32_t x = 0; x < columns; ++x)
            for (int c = 0; c < 4; ++c)
                worst = std::max(worst, std::abs(
                    int(a.rgba[((size_t(y) * a.width) + ax + x) * 4 + c])
                    - int(b.rgba[((size_t(y) * b.width) + bx + x) * 4 + c])));
    return worst;
}

struct CameraTest : Corrade::TestSuite::Tester {
    explicit CameraTest();

    void extensionsListed();
    void imageRegionDefault();
    void imageRegionCenterCrop();
    void imageRegionOffAxis();
    void imageRegionVertical();
    void imageRegionOrthographic();
    void imageRegionInvalid();
};

CameraTest::CameraTest()
{
    addTests({&CameraTest::extensionsListed,
        &CameraTest::imageRegionDefault,
        &CameraTest::imageRegionCenterCrop,
        &CameraTest::imageRegionOffAxis,
        &CameraTest::imageRegionVertical,
        &CameraTest::imageRegionOrthographic,
        &CameraTest::imageRegionInvalid});
}

constexpr uint32_t W = 96, H = 64;

// Two renders of the very same frame differ by up to two levels per channel
// (Filament dithers), so "the same picture" means within this.
constexpr int Noise = 2;
constexpr float Aspect = float(W) / float(H);

void CameraTest::extensionsListed()
{
    DeviceFixture f;
    CORRADE_VERIFY(f.library);

    // imageRegion is part of the two standard camera extensions; a host can
    // ask for them before relying on an off-axis frustum.
    bool perspective = false, orthographic = false;
    const char *const *extensions = anariGetDeviceExtensions(f.library, "default");
    CORRADE_VERIFY(extensions);
    for (const char *const *ext = extensions; *ext; ++ext) {
        perspective |= std::string(*ext) == "ANARI_KHR_CAMERA_PERSPECTIVE";
        orthographic |= std::string(*ext) == "ANARI_KHR_CAMERA_ORTHOGRAPHIC";
    }
    CORRADE_VERIFY(perspective);
    CORRADE_VERIFY(orthographic);
}

void CameraTest::imageRegionDefault()
{
    DeviceFixture f;
    CORRADE_VERIFY(f.device);
    Scene scene(f.device);

    const Image plain = scene.render(W, H, Aspect, nullptr);
    CORRADE_COMPARE(plain.rgba.size(), size_t(W) * H * 4);
    const float whole[] = {0.0f, 0.0f, 1.0f, 1.0f};
    const Image explicitWhole = scene.render(W, H, Aspect, whole);
    CORRADE_COMPARE_AS(maxDifference(plain, 0, explicitWhole, 0, W), Noise,
        Corrade::TestSuite::Compare::LessOrEqual);
}

void CameraTest::imageRegionCenterCrop()
{
    DeviceFixture f;
    CORRADE_VERIFY(f.device);
    Scene scene(f.device);

    // The middle half of the image plane, drawn into a frame half as wide:
    // the same pixels as the middle columns of the whole picture.
    const Image whole = scene.render(W, H, Aspect, nullptr);
    const float middle[] = {0.25f, 0.0f, 0.75f, 1.0f};
    const Image crop = scene.render(W / 2, H, Aspect, middle);
    CORRADE_COMPARE(crop.width, W / 2);
    CORRADE_COMPARE(crop.height, H);
    CORRADE_COMPARE_AS(maxDifference(whole, W / 4, crop, 0, W / 2), Noise,
        Corrade::TestSuite::Compare::LessOrEqual);
}

void CameraTest::imageRegionOffAxis()
{
    DeviceFixture f;
    CORRADE_VERIFY(f.device);
    Scene scene(f.device);

    // A box that starts left of the image plane: the picture is the whole
    // one moved a quarter of its width to the right, with new content on the
    // left. No column is black, because the frustum grew instead of the
    // picture being cropped.
    const Image whole = scene.render(W, H, Aspect, nullptr);
    const float shifted[] = {-0.25f, 0.0f, 0.75f, 1.0f};
    const Image moved = scene.render(W, H, Aspect, shifted);
    CORRADE_COMPARE_AS(maxDifference(whole, 0, moved, W / 4, W - W / 4), Noise,
        Corrade::TestSuite::Compare::LessOrEqual);
    for (uint32_t x = 0; x < W / 4; ++x) {
        const uint8_t *p = &moved.rgba[((H / 2) * W + x) * 4];
        CORRADE_VERIFY(p[0] || p[1] || p[2]);
    }
}

void CameraTest::imageRegionVertical()
{
    DeviceFixture f;
    CORRADE_VERIFY(f.device);
    Scene scene(f.device);

    // The box's second pair of numbers is the vertical extent, from the lower
    // edge of the image plane. A box that starts a quarter of the height below
    // it moves the picture up by that much. The quad's colour grows towards
    // the top, which tells the row order of the mapped frame.
    const Image whole = scene.render(W, H, Aspect, nullptr);
    const float lower[] = {0.0f, -0.25f, 1.0f, 0.75f};
    const Image moved = scene.render(W, H, Aspect, lower);
    auto green = [](const Image &image, uint32_t row) {
        return int(image.rgba[(size_t(row) * image.width + image.width / 2) * 4 + 1]);
    };
    const bool bottomUp = green(whole, 0) < green(whole, H - 1);
    // Rows counted from the top of the picture
    auto top = [&](uint32_t row) { return bottomUp ? H - 1 - row : row; };
    int worst = 0;
    for (uint32_t r = 0; r + H / 4 < H; ++r)
        for (uint32_t x = 0; x < W; ++x)
            for (int c = 0; c < 4; ++c)
                worst = std::max(worst, std::abs(
                    int(moved.rgba[((size_t(top(r)) * W) + x) * 4 + c])
                    - int(whole.rgba[((size_t(top(r + H / 4)) * W) + x) * 4 + c])));
    CORRADE_COMPARE_AS(worst, Noise, Corrade::TestSuite::Compare::LessOrEqual);
}

void CameraTest::imageRegionOrthographic()
{
    DeviceFixture f;
    CORRADE_VERIFY(f.device);
    Scene scene(f.device);

    const Image whole = scene.render(W, H, Aspect, nullptr, "orthographic");
    const float middle[] = {0.25f, 0.0f, 0.75f, 1.0f};
    const Image crop = scene.render(W / 2, H, Aspect, middle, "orthographic");
    CORRADE_COMPARE_AS(maxDifference(whole, W / 4, crop, 0, W / 2), Noise,
        Corrade::TestSuite::Compare::LessOrEqual);
}

void CameraTest::imageRegionInvalid()
{
    DeviceFixture f;
    CORRADE_VERIFY(f.device);
    Scene scene(f.device);

    // An empty box is reported by the camera (as an error to the device's
    // status callback) and the whole image is used, never a silent crop.
    const Image whole = scene.render(W, H, Aspect, nullptr);
    const float empty[] = {0.5f, 0.0f, 0.5f, 1.0f};
    const Image bad = scene.render(W, H, Aspect, empty);
    CORRADE_COMPARE_AS(maxDifference(whole, 0, bad, 0, W), Noise,
        Corrade::TestSuite::Compare::LessOrEqual);
}

}

CORRADE_TEST_MAIN(CameraTest)
