#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#define VC_EXTRALEAN        // Exclude rarely-used stuff from Windows headers

#include <wincodec.h>
#include <atlcomcli.h>

#include <iostream>
#include <memory>
#include <filesystem>

#define NANOSVG_IMPLEMENTATION
#pragma warning(push)
#pragma warning(disable:4244)
#include "../external/nanosvg/src/nanosvg.h"
#pragma warning(pop)
#define NANOSVGRAST_IMPLEMENTATION
#include "../external/nanosvg/src/nanosvgrast.h"

#include "../revision.h"
#ifndef REV_NUM
#define REV_NUM 0
#endif

#pragma comment(lib, "windowscodecs.lib")

// simple convert ANSI string to wide character string
inline const std::wstring A2WStr(const std::string_view& sv)
{
	return std::wstring(sv.begin(), sv.end());
}

HRESULT NSVGimageToCImage(char* data, IWICImagingFactory* pWICFactory, IWICBitmap **ppWICBitmap, float scale)
{
	NSVGimage* svgImage = nsvgParse(data, "px", 96.0f);

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

int wmain(int argc, wchar_t* argv[])
{
	const std::wstring ver = L"png2svg 0.2 N-" + std::to_wstring(REV_NUM);
	std::wcout << ver << L"\n";

	if (argc > 1)
	{
		HRESULT hr = S_OK;
		std::wstring error;
		std::wstring input_filename = argv[1];
		FILE* file = nullptr;
		CComPtr<IWICImagingFactory> pWICFactory;
		CComPtr<IWICBitmap> pWICBitmap;

		try {
			if (!std::filesystem::exists(input_filename)) {
				error = L"The file \"" + input_filename + L"\" is missing!";
				throw std::exception();
			}

			if (_wfopen_s(&file, input_filename.c_str(), L"rb") != 0) {
				error = L"The file \"" + input_filename + L"\" could not be opened!";
				throw std::exception();
			}

			fseek(file, 0, SEEK_END);
			size_t fsize = ftell(file);
			fseek(file, 0, SEEK_SET);

			std::unique_ptr<char[]> fdata(new(std::nothrow) char[fsize + 1]);
			if (!fdata) {
				error = L"Memory allocation error!";
				throw std::exception();
			}

			if (fread(fdata.get(), 1, fsize, file) != fsize) {
				error = L"The file \"" + input_filename + L"\" could not be read!";
				throw std::exception();
			}

			fdata[fsize] = '\0'; // Must be null terminated.

			fclose(file);
			file = nullptr;

			hr = CoInitializeEx(nullptr, COINITBASE_MULTITHREADED);
			if (FAILED(hr)) {
				error = L"COM initialization failed!";
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
				error = L"WIC initialization failed!";
				throw std::exception();
			}

			hr = NSVGimageToCImage(fdata.get(), pWICFactory, &pWICBitmap, 1);
			if (FAILED(hr)) {
				error = L"Reading SVG file failed!";
				throw std::exception();
			}

			CComPtr<IWICStream> pStream;
			CComPtr<IWICBitmapEncoder> pEncoder;
			CComPtr<IWICBitmapFrameEncode> pFrameEncode;
			WICPixelFormatGUID format = GUID_WICPixelFormatDontCare;
			GUID_ContainerFormatPng;

			std::wstring output_filename = input_filename + L".png";
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
				error = L"PNG file write failed!";
				throw std::exception();
			}

			std::wcout << L"The PNG file was written successfully!\n";
		}
		catch (...) {
			std::wcout << L"ERROR:" << error << L"\n";
		}

		if (file) {
			fclose(file);
			file = nullptr;
		}

	}

	system("pause");
}
