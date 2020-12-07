#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#define VC_EXTRALEAN        // Exclude rarely-used stuff from Windows headers

#include <atlimage.h>

#include <iostream>
#include <filesystem>

#define NANOSVG_IMPLEMENTATION
#pragma warning(push)
#pragma warning(disable:4244)
#include "../external/nanosvg/src/nanosvg.h"
#pragma warning(pop)
#define NANOSVGRAST_IMPLEMENTATION
#include "../external/nanosvg/src/nanosvgrast.h"

// simple convert ANSI string to wide character string
inline const std::wstring A2WStr(const std::string_view& sv)
{
    return std::wstring(sv.begin(), sv.end());
}

HRESULT NSVGimageToCImage(LPCSTR filename, CImage& image, float scale)
{
    NSVGimage* svgImage = nsvgParseFromFile(filename, "px", 96.0f);

    image.Destroy();

    if (!svgImage) {
        return E_FAIL;
    }

    NSVGrasterizer* rasterizer = nsvgCreateRasterizer();
    if (!rasterizer) {
        nsvgDelete(svgImage);
        return E_FAIL;
    }

    if (!image.Create(int(svgImage->width * scale), int(svgImage->height * scale), 32)) {
        nsvgDeleteRasterizer(rasterizer);
        nsvgDelete(svgImage);
        return E_FAIL;
    }

    nsvgRasterize(rasterizer, svgImage, 0.0f, 0.0f, scale,
        static_cast<unsigned char*>(image.GetBits()),
        image.GetWidth(), image.GetHeight(), image.GetPitch());

    // NanoSVG outputs RGBA but we need BGRA so we swap red and blue
    BYTE* bits = static_cast<BYTE*>(image.GetBits());
    for (int y = 0; y < image.GetHeight(); y++, bits += image.GetPitch()) {
        RGBQUAD* p = reinterpret_cast<RGBQUAD*>(bits);
        for (int x = 0; x < image.GetWidth(); x++) {
            std::swap(p[x].rgbRed, p[x].rgbBlue);
        }
    }

    nsvgDeleteRasterizer(rasterizer);
    nsvgDelete(svgImage);

    return S_OK;
}

int main(int argc, char* argv[])
{
    std::cout << "png2svg 0.1\n";

    if (argc > 1) 
    {
        std::string input_filename = argv[1];

        if (std::filesystem::exists(input_filename))
        {
            CImage image;
            HRESULT hr = NSVGimageToCImage(input_filename.c_str(), image, 1);
            if (SUCCEEDED(hr))
            {
                std::wstring output_filename = A2WStr(input_filename) + L".png";

                hr = image.Save(output_filename.c_str());
                if (SUCCEEDED(hr))
                {
                    std::cout << "The PNG file was written successfully!\n";
                }
                else
                {
                    std::cout << "Error writing PNG file!\n";
                }
            }
            else
            {
                std::cout << "Error reading SVG file!\n";
            }
        }
        else
        {
            std::cout << "The input file \"" << input_filename << "\" is missing!\n";
        }
    }

    system("pause");
}
