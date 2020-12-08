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

bool StrToUInt32(const wchar_t* str, uint32_t& value)
{
	wchar_t* end;
	uint32_t v = wcstoul(str, &end, 10);
	if (end > str) {
		value = v;
		return true;
	}
	return false;
}

bool StrToFloat(const wchar_t* str, float& value)
{
	wchar_t* end;
	float v = wcstof(str, &end);
	if (end > str) {
		value = v;
		return true;
	}
	return false;
}

int wmain(int argc, wchar_t* argv[])
{
	const std::wstring ver = L"png2svg 0.2 N-" + std::to_wstring(REV_NUM);
	std::wcout << ver << L"\n";

	if (argc > 1)
	{
		std::wstring input_filename = argv[1];
		std::wstring output_filename = input_filename + L".png";

		HRESULT hr = S_OK;
		std::wstring error;

		uint32_t w = 0;
		uint32_t h = 0;
		float scale = 1.0;

		FILE* file = nullptr;

		NSVGimage* svgImage = nullptr;
		NSVGrasterizer* rasterizer = nullptr;

		CComPtr<IWICImagingFactory> pWICFactory;
		CComPtr<IWICBitmap> pWICBitmap;

		try {
			for (int i = 2; i < argc; i++) {
				if (wcscmp(argv[i], L"-w") == 0) {
					if (i + 1 >= argc || !StrToUInt32(argv[i+1], w) || w == 0) {
						error = L"Invalid width value!";
						throw std::exception();
					}
					i++;
				}
				else if (wcscmp(argv[i], L"-h") == 0) {
					if (i + 1 >= argc || !StrToUInt32(argv[i+1], h) || h == 0) {
						error = L"Invalid height value!";
						throw std::exception();
					}
					i++;
				}
				else if (wcscmp(argv[i], L"-scale") == 0) {
					if (i + 1 >= argc || !StrToFloat(argv[i+1], scale) || scale <= 0) {
						error = L"Invalid scale value!";
						throw std::exception();
					}
					i++;
				}
				else if (i + 1 == argc) {
					output_filename = argv[i];
				}
				else {
					error = L"Invalid command line format!";
					throw std::exception();
				}
			}

			if (w && h || scale !=1 && (w || h)) {
				error = L"Only one '-w', '-h', or '-scale' option is allowed!";
				throw std::exception();
			}

			std::vector<char> buf;

			if (wcscmp(input_filename.c_str(), L"-") == 0) {
				const size_t block_size = 1024;
				size_t len = 0;
				do {
					buf.resize(buf.size() + block_size);
					std::cin.read(buf.data() + len, block_size);
					len += (size_t)std::cin.gcount();
				} while (len == buf.size());

				buf.resize(len+1);
			}
			else if (_wfopen_s(&file, input_filename.c_str(), L"rb") == 0) {
				fseek(file, 0, SEEK_END);
				size_t len = ftell(file);
				fseek(file, 0, SEEK_SET);

				buf.resize(len+1);

				size_t ret = fread(buf.data(), 1, len, file);
				fclose(file);
				file = nullptr;

				if (ret != len) {
					error = L"The file \"" + input_filename + L"\" could not be read!";
					throw std::exception();
				}
			}

			buf[buf.size()-1] = '\0'; // Must be null terminated.

			svgImage = nsvgParse(buf.data(), "px", 96.0f);
			if (svgImage) {
				rasterizer = nsvgCreateRasterizer();
			}
			if (!svgImage || !rasterizer) {
				error = L"SVG parsing failed!";
				throw std::exception();
			}

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

			if (!w && !h) {
				w = (uint32_t)std::round(svgImage->width * scale);
				h = (uint32_t)std::round(svgImage->height * scale);
			}
			else if (!w) {
				scale = h / svgImage->height;
				w = (uint32_t)std::round(svgImage->width * scale);
			}
			else if (!h) {
				scale = w / svgImage->width;
				h = (uint32_t)std::round(svgImage->height * scale);
			}

			hr = pWICFactory->CreateBitmap(
				w,
				h,
				GUID_WICPixelFormat32bppPRGBA,
				WICBitmapCacheOnDemand,
				&pWICBitmap);
			if (FAILED(hr)) {
				error = L"WIC bitmap creation failed!";
				throw std::exception();
			}

			CComPtr<IWICBitmapLock> pWICBitmapLock;
			WICRect rcLock = { 0, 0, (INT)w, (INT)h };
			UINT uStride = 0;
			BYTE* pData = nullptr;

			hr = pWICBitmap->Lock(&rcLock, WICBitmapLockWrite, &pWICBitmapLock);
			if (SUCCEEDED(hr)) {
				hr = pWICBitmapLock->GetStride(&uStride);
			}
			if (SUCCEEDED(hr)) {
				UINT cbBufferSize = 0;
				hr = pWICBitmapLock->GetDataPointer(&cbBufferSize, &pData);
			}
			if (SUCCEEDED(hr)) {
				nsvgRasterize(rasterizer, svgImage, 0.0f, 0.0f, scale, pData, w, h, uStride);
			}
			if (FAILED(hr)) {
				error = L"Converting SVG to bitmap failed!";
				throw std::exception();
			}

			pWICBitmapLock.Release();

			nsvgDeleteRasterizer(rasterizer);
			rasterizer = nullptr;
			nsvgDelete(svgImage);
			svgImage = nullptr;

			CComPtr<IWICStream> pStream;
			CComPtr<IWICBitmapEncoder> pEncoder;
			CComPtr<IWICBitmapFrameEncode> pFrameEncode;
			WICPixelFormatGUID format = GUID_WICPixelFormatDontCare;
			GUID_ContainerFormatPng;

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

		if (rasterizer) {
			nsvgDeleteRasterizer(rasterizer);
			rasterizer = nullptr;
		}

		if (svgImage) {
			nsvgDelete(svgImage);
			svgImage = nullptr;
		}
	}

	Sleep(2000);
}
