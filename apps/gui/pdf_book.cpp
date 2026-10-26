#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "book_pages.hpp"
#include "../../src/long_path.hpp"
#include <inspectable.h>
#include <roapi.h>
#include <shcore.h>
#include <wincodec.h>
#include <winstring.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

// Windows.Data.Pdf is a Windows Runtime API. MinGW has no header for it, so the few interfaces used here are
// declared by hand, as in the Windows SDK's windows.data.pdf.h: same IIDs, same methods in the same order. Methods
// that are never called only hold their place in the table.
namespace pdf_abi
{
    enum AsyncState { kStarted = 0, kCompleted = 1, kCanceled = 2, kError = 3 };

    struct SizeF { float width; float height; };
    struct RectF { float x; float y; float width; float height; };
    struct ColorArgb { BYTE a; BYTE r; BYTE g; BYTE b; };

    // Only passed around: the file, and the memory a page is drawn into.
    struct IRandomAccessStream : public IInspectable {};

    struct IAsyncInfo : public IInspectable
    {
        virtual HRESULT STDMETHODCALLTYPE get_Id(UINT32 *id) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_Status(int *status) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT *code) = 0;
        virtual HRESULT STDMETHODCALLTYPE Cancel() = 0;
        virtual HRESULT STDMETHODCALLTYPE Close() = 0;
    };

    struct IAsyncAction;

    struct IAsyncActionCompletedHandler : public IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE Invoke(IAsyncAction *action, int status) = 0;
    };

    struct IAsyncAction : public IInspectable
    {
        virtual HRESULT STDMETHODCALLTYPE put_Completed(IAsyncActionCompletedHandler *handler) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_Completed(IAsyncActionCompletedHandler **handler) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetResults() = 0;
    };

    struct IPdfPageRenderOptions : public IInspectable
    {
        virtual HRESULT STDMETHODCALLTYPE get_SourceRect(RectF *value) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_SourceRect(RectF value) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_DestinationWidth(UINT32 *value) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_DestinationWidth(UINT32 value) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_DestinationHeight(UINT32 *value) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_DestinationHeight(UINT32 value) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_BackgroundColor(ColorArgb *value) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_BackgroundColor(ColorArgb value) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_IsIgnoringHighContrast(boolean *value) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_IsIgnoringHighContrast(boolean value) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_BitmapEncoderId(GUID *value) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_BitmapEncoderId(GUID value) = 0;
    };

    struct IPdfPage : public IInspectable
    {
        virtual HRESULT STDMETHODCALLTYPE RenderToStreamAsync(IRandomAccessStream *output, IAsyncAction **action) = 0;
        virtual HRESULT STDMETHODCALLTYPE RenderWithOptionsToStreamAsync(IRandomAccessStream *output,
            IPdfPageRenderOptions *options, IAsyncAction **action) = 0;
        virtual HRESULT STDMETHODCALLTYPE PreparePageAsync(IAsyncAction **action) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_Index(UINT32 *value) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_Size(SizeF *value) = 0;
    };

    struct IPdfDocument : public IInspectable
    {
        virtual HRESULT STDMETHODCALLTYPE GetPage(UINT32 index, IPdfPage **page) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_PageCount(UINT32 *value) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_IsPasswordProtected(boolean *value) = 0;
    };

    // IAsyncOperation<PdfDocument> and its handler: their IIDs are the ones Windows derives for that type.
    struct IPdfDocumentOperation;

    struct IPdfDocumentCompletedHandler : public IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE Invoke(IPdfDocumentOperation *operation, int status) = 0;
    };

    struct IPdfDocumentOperation : public IInspectable
    {
        virtual HRESULT STDMETHODCALLTYPE put_Completed(IPdfDocumentCompletedHandler *handler) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_Completed(IPdfDocumentCompletedHandler **handler) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetResults(IPdfDocument **document) = 0;
    };

    struct IPdfDocumentStatics : public IInspectable
    {
        virtual HRESULT STDMETHODCALLTYPE LoadFromFileAsync(IInspectable *file, IPdfDocumentOperation **operation) = 0;
        virtual HRESULT STDMETHODCALLTYPE LoadFromFileWithPasswordAsync(IInspectable *file, HSTRING password,
            IPdfDocumentOperation **operation) = 0;
        virtual HRESULT STDMETHODCALLTYPE LoadFromStreamAsync(IRandomAccessStream *input,
            IPdfDocumentOperation **operation) = 0;
    };
}

// What __uuidof finds for each, as MIDL_INTERFACE gives it in the SDK.
__CRT_UUID_DECL(pdf_abi::IRandomAccessStream, 0x905a0fe1, 0xbc53, 0x11df, 0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda)
__CRT_UUID_DECL(pdf_abi::IAsyncInfo, 0x00000036, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46)
__CRT_UUID_DECL(pdf_abi::IAsyncActionCompletedHandler, 0xa4ed5c81, 0x76c9, 0x40bd, 0x8b, 0xe6, 0xb1, 0xd9, 0x0f, 0xb2, 0x0a, 0xe7)
__CRT_UUID_DECL(pdf_abi::IAsyncAction, 0x5a648006, 0x843a, 0x4da9, 0x86, 0x5b, 0x9d, 0x26, 0xe5, 0xdf, 0xad, 0x7b)
__CRT_UUID_DECL(pdf_abi::IPdfPageRenderOptions, 0x3c98056f, 0xb7cf, 0x4c29, 0x9a, 0x04, 0x52, 0xd9, 0x02, 0x67, 0xf4, 0x25)
__CRT_UUID_DECL(pdf_abi::IPdfPage, 0x9db4b0c8, 0x5320, 0x4cfc, 0xad, 0x76, 0x49, 0x3f, 0xda, 0xd0, 0xe5, 0x94)
__CRT_UUID_DECL(pdf_abi::IPdfDocument, 0xac7ebedd, 0x80fa, 0x4089, 0x84, 0x6e, 0x81, 0xb7, 0x7f, 0xf5, 0xa8, 0x6c)
__CRT_UUID_DECL(pdf_abi::IPdfDocumentCompletedHandler, 0x8d4950b3, 0x629d, 0x5d7d, 0x84, 0xcc, 0x04, 0xc0, 0xdc, 0xf7, 0x94, 0x2b)
__CRT_UUID_DECL(pdf_abi::IPdfDocumentOperation, 0xd6b166ec, 0x099a, 0x5ee2, 0xad, 0x2e, 0xf4, 0xc8, 0x86, 0x14, 0xaa, 0xbb)
__CRT_UUID_DECL(pdf_abi::IPdfDocumentStatics, 0x433a0b5f, 0xc007, 0x4788, 0x90, 0xf2, 0x08, 0x14, 0x3d, 0x92, 0x25, 0x99)

namespace
{
    using namespace pdf_abi;

    // FileAccessMode.Read
    const DWORD kReadAccess = 0;

    // Waits for an asynchronous operation of the Windows Runtime: a handler that only signals an event. It is agile,
    // so Windows calls it on its own thread and never needs this thread's apartment meanwhile.
    template <class Handler, class Operation>
    class Completion final : public Handler
    {
    public:
        Completion() : m_event(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
        ~Completion() { if (m_event) CloseHandle(m_event); }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override
        {
            if (iid == __uuidof(IUnknown) || iid == __uuidof(Handler) || iid == __uuidof(IAgileObject))
            {
                *object = static_cast<Handler *>(this);
                AddRef();
                return S_OK;
            }
            *object = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refs); }
        ULONG STDMETHODCALLTYPE Release() override
        {
            ULONG refs = InterlockedDecrement(&m_refs);
            if (refs == 0)
                delete this;
            return refs;
        }
        HRESULT STDMETHODCALLTYPE Invoke(Operation *, int) override
        {
            SetEvent(m_event);
            return S_OK;
        }

        HANDLE Event() const { return m_event; }

    private:
        LONG m_refs = 1;
        HANDLE m_event;
    };

    // True once 'operation' completed; false when it failed or was cancelled.
    template <class Handler, class Operation>
    bool Await(Operation *operation)
    {
        ComPtr<Completion<Handler, Operation>> completion;
        completion.Attach(new Completion<Handler, Operation>());
        ComPtr<IAsyncInfo> info;
        if (!completion->Event() || FAILED(operation->QueryInterface(IID_PPV_ARGS(info.GetAddressOf()))) ||
            FAILED(operation->put_Completed(completion.Get())))
            return false;
        DWORD index = 0;
        HANDLE event = completion->Event();
        // Waits like COM does on a single-threaded apartment: calls into it still get through.
        if (FAILED(CoWaitForMultipleHandles(0, INFINITE, 1, &event, &index)))
            return false;
        int status = kError;
        return SUCCEEDED(info->get_Status(&status)) && status == kCompleted;
    }

    template <class Interface>
    HRESULT Activate(const wchar_t *className, ComPtr<Interface> &out)
    {
        HSTRING_HEADER header;
        HSTRING name = nullptr;
        HRESULT hr = WindowsCreateStringReference(className, static_cast<UINT32>(wcslen(className)), &header, &name);
        ComPtr<IInspectable> instance;
        if (SUCCEEDED(hr))
            hr = RoActivateInstance(name, instance.GetAddressOf());
        if (SUCCEEDED(hr))
            hr = instance.As(&out);
        return hr;
    }

    class PdfBook : public BookPages
    {
    public:
        PdfBook(IWICImagingFactory *wic, ComPtr<IPdfDocument> document, UINT32 count)
            : m_wic(wic), m_document(std::move(document)), m_count(count) {}

        size_t Count() const override { return m_count; }

        // Drawn as a BMP into memory, which WIC then reads: no compression either way.
        bool Render(size_t index, UINT width, UINT maxSide, DecodedFrame &out) override
        {
            ComPtr<IPdfPage> page;
            SizeF size = {};
            if (index >= m_count || FAILED(m_document->GetPage(static_cast<UINT32>(index), page.GetAddressOf())) ||
                FAILED(page->get_Size(&size)) || !(size.width > 0.0f) || !(size.height > 0.0f))
                return false;

            double scale = static_cast<double>(width) / size.width;
            scale = std::min(scale, std::min(maxSide / static_cast<double>(size.width), maxSide / static_cast<double>(size.height)));
            scale = std::min(scale, std::sqrt(kMaxPagePixels / (static_cast<double>(size.width) * size.height)));
            // The renderer multiplies the size asked for by a display scale of its own (140% on a 144 DPI screen,
            // say), which the first page measures; the size is asked for divided by it.
            scale /= m_renderScale;
            UINT32 pixelWidth = std::max(1u, static_cast<UINT32>(std::lround(size.width * scale)));
            UINT32 pixelHeight = std::max(1u, static_cast<UINT32>(std::lround(size.height * scale)));

            ComPtr<IPdfPageRenderOptions> options;
            ComPtr<IRandomAccessStream> memory;
            ComPtr<IAsyncAction> action;
            if (FAILED(Activate(L"Windows.Data.Pdf.PdfPageRenderOptions", options)) ||
                FAILED(options->put_DestinationWidth(pixelWidth)) || FAILED(options->put_DestinationHeight(pixelHeight)) ||
                FAILED(options->put_BitmapEncoderId(CLSID_WICBmpEncoder)) ||
                FAILED(Activate(L"Windows.Storage.Streams.InMemoryRandomAccessStream", memory)) ||
                FAILED(page->RenderWithOptionsToStreamAsync(memory.Get(), options.Get(), action.GetAddressOf())) ||
                !Await<IAsyncActionCompletedHandler>(action.Get()) || FAILED(action->GetResults()))
                return false;

            ComPtr<IStream> stream;
            LARGE_INTEGER start = {};
            if (FAILED(CreateStreamOverRandomAccessStream(memory.Get(), IID_PPV_ARGS(stream.GetAddressOf()))) ||
                FAILED(stream->Seek(start, STREAM_SEEK_SET, nullptr)))
                return false;
            if (!DecodePicture(m_wic, stream.Get(), UINT_MAX, maxSide, out))
                return false;
            if (out.sourceWidth > 0)
                m_renderScale = static_cast<double>(out.sourceWidth) / pixelWidth;
            // A page reads as the size it was drawn at.
            out.sourceWidth = out.width;
            out.sourceHeight = out.height;
            return true;
        }

    private:
        // A page is drawn at most this big: 96 MB in memory, enough for a page as wide as a 5K screen.
        static constexpr double kMaxPagePixels = 24.0 * 1024 * 1024;

        IWICImagingFactory *m_wic;
        ComPtr<IPdfDocument> m_document;
        UINT32 m_count;
        double m_renderScale = 1.0;
    };
}

std::unique_ptr<BookPages> OpenPdfBook(IWICImagingFactory *wic, const std::wstring &path)
{
    HSTRING_HEADER header;
    HSTRING name = nullptr;
    const wchar_t *className = L"Windows.Data.Pdf.PdfDocument";
    ComPtr<IPdfDocumentStatics> statics;
    ComPtr<IRandomAccessStream> file;
    ComPtr<IPdfDocumentOperation> operation;
    ComPtr<IPdfDocument> document;
    UINT32 count = 0;
    if (FAILED(WindowsCreateStringReference(className, static_cast<UINT32>(wcslen(className)), &header, &name)) ||
        FAILED(RoGetActivationFactory(name, IID_PPV_ARGS(statics.GetAddressOf()))) ||
        // A path past MAX_PATH only opens with the \\?\ prefix.
        FAILED(CreateRandomAccessStreamOnFile((path.size() < MAX_PATH ? path : long_path(path)).c_str(), kReadAccess,
            IID_PPV_ARGS(file.GetAddressOf()))) ||
        FAILED(statics->LoadFromStreamAsync(file.Get(), operation.GetAddressOf())) ||
        !Await<IPdfDocumentCompletedHandler>(operation.Get()) || FAILED(operation->GetResults(document.GetAddressOf())) ||
        !document || FAILED(document->get_PageCount(&count)) || count == 0)
        return nullptr;
    std::unique_ptr<BookPages> book(new PdfBook(wic, std::move(document), count));
    // A small first page measures the renderer's display scale, so the pages read later come out the size asked.
    DecodedFrame probe;
    book->Render(0, 64, 64, probe);
    return book;
}
