#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#define VC_EXTRALEAN        // Exclude rarely-used stuff from Windows headers

#include <wincodec.h>
#include <atlcomcli.h>

#include <iostream>
#include <filesystem>

#define NANOSVG_IMPLEMENTATION
#pragma warning(push)
#pragma warning(disable:4244)
#include "../external/nanosvg/src/nanosvg.h"
#pragma warning(pop)
#define NANOSVGRAST_IMPLEMENTATION
#include "../external/nanosvg/src/nanosvgrast.h"

#pragma comment(lib, "windowscodecs.lib")

// simple convert ANSI string to wide character string
inline const std::wstring A2WStr(const std::string_view& sv)
{
	return std::wstring(sv.begin(), sv.end());
}

HRESULT NSVGimageToCImage(LPCSTR filename, IWICImagingFactory* pWICFactory, IWICBitmap **ppWICBitmap, float scale)
{
	NSVGimage* svgImage = nsvgParseFromFile(filename, "px", 96.0f);

	if (!svgImage) {
		return E_FAIL;
	}

	NSVGrasterizer* rasterizer = nsvgCreateRasterizer();
	if (!rasterizer) {
		nsvgDelete(svgImage);
		return E_FAIL;
	}

	int w = (int)std::round(svgImage->width * scale);
	int h = (int)std::round(svgImage->height * scale);

	HRESULT hr = pWICFactory->CreateBitmap(
		w,
		h,
		GUID_WICPixelFormat32bppPRGBA,
		WICBitmapCacheOnDemand,
		ppWICBitmap);
	if (FAILED(hr)) {
		nsvgDeleteRasterizer(rasterizer);
		nsvgDelete(svgImage);
		return E_FAIL;
	}

	CComPtr<IWICBitmapLock> pWICBitmapLock;
	WICRect rcLock = { 0, 0, w, h };
	UINT uStride = 0;
	BYTE* pData = nullptr;

	hr = (*ppWICBitmap)->Lock(&rcLock, WICBitmapLockWrite, &pWICBitmapLock);
	if (S_OK == hr) {
		hr = pWICBitmapLock->GetStride(&uStride);
		if (S_OK == hr) {
			UINT cbBufferSize = 0;
			hr = pWICBitmapLock->GetDataPointer(&cbBufferSize, &pData);
		}
	}

	if (SUCCEEDED(hr)) {
		nsvgRasterize(rasterizer, svgImage, 0.0f, 0.0f, scale, pData, w, h, uStride);
	}

	pWICBitmapLock.Release();

	nsvgDeleteRasterizer(rasterizer);
	nsvgDelete(svgImage);

	return S_OK;
}

int main(int argc, char* argv[])
{
	std::cout << "png2svg 0.1\n";

	if (argc > 1) 
	{
		HRESULT hr = S_OK;
		std::string error;
		std::string input_filename = argv[1];
		CComPtr<IWICImagingFactory> pWICFactory;
		CComPtr<IWICBitmap> pWICBitmap;

		try {
			if (!std::filesystem::exists(input_filename)) {
				error = "The input file \"" + input_filename + "\" is missing!";
				throw std::exception();
			}

			hr = CoInitializeEx(nullptr, COINITBASE_MULTITHREADED);
			if (FAILED(hr)) {
				error = "COM initialization failed!";
				throw std::exception();
			}

			hr = CoCreateInstance(
				CLSID_WICImagingFactory1, // we use CLSID_WICImagingFactory1 to support Windows 7 without Platform Update
				nullptr,
				CLSCTX_INPROC_SERVER,
				IID_IWICImagingFactory,
				(LPVOID*)&pWICFactory
			);
			if (FAILED(hr)) {
				error = "WIC initialization failed!";
				throw std::exception();
			}

			hr = NSVGimageToCImage(input_filename.c_str(), pWICFactory, &pWICBitmap, 1);
			if (FAILED(hr)) {
				error = "Reading SVG file failed!";
				throw std::exception();
			}

			CComPtr<IWICStream> pStream;
			CComPtr<IWICBitmapEncoder> pEncoder;
			CComPtr<IWICBitmapFrameEncode> pFrameEncode;
			WICPixelFormatGUID format = GUID_WICPixelFormatDontCare;
			GUID_ContainerFormatPng;

			std::wstring output_filename = A2WStr(input_filename) + L".png";
			UINT w, h;

			hr = pWICBitmap->GetSize(&w, &h);
			if (SUCCEEDED(hr)) {
				hr = pWICFactory->CreateStream(&pStream);
			}
			if (SUCCEEDED(hr)) {
				hr = pStream->InitializeFromFilename(output_filename.c_str(), GENERIC_WRITE);
			}
			if (SUCCEEDED(hr)) {
				hr = pWICFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &pEncoder);
			}
			if (SUCCEEDED(hr)) {
				hr = pEncoder->Initialize(pStream, WICBitmapEncoderNoCache);
			}
			if (SUCCEEDED(hr)) {
				hr = pEncoder->CreateNewFrame(&pFrameEncode, nullptr);
			}
			if (SUCCEEDED(hr)) {
				hr = pFrameEncode->Initialize(nullptr);
			}
			if (SUCCEEDED(hr)) {
				hr = pFrameEncode->SetSize(w, h);
			}
			if (SUCCEEDED(hr)) {
				hr = pFrameEncode->SetPixelFormat(&format);
			}
			if (SUCCEEDED(hr)) {
				hr = pFrameEncode->WriteSource(pWICBitmap, nullptr);
			}
			if (SUCCEEDED(hr)) {
				hr = pFrameEncode->Commit();
			}
			if (SUCCEEDED(hr)) {
				hr = pEncoder->Commit();
			}

			if (FAILED(hr)) {
				error = "PNG file write failed!";
				throw std::exception();
			}

			std::cout << "The PNG file was written successfully!\n";
		}
		catch (...) {
			std::cout << "ERROR:" << error << "\n";
		}
	}

	system("pause");
}
