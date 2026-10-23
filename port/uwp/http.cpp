#include "http.h"

#include <chrono>
#include <cstring>
#include <string>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>

extern "C" int PortUwp_HttpRequest(const char *url, const char *body,
                                    const char *const *headers, char *answer, size_t capacity) {
    if (!url || !answer || capacity < 2) return 0;
    answer[0] = '\0';
    try {
        static thread_local bool apartment = [] {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            return true;
        }();
        (void)apartment;
        namespace http = winrt::Windows::Web::Http;
        http::HttpClient client;
        http::HttpRequestMessage request(
            body ? http::HttpMethod::Post() : http::HttpMethod::Get(),
            winrt::Windows::Foundation::Uri(winrt::to_hstring(url)));
        request.Headers().TryAppendWithoutValidation(L"User-Agent", L"Tenkaichi3Decomp-Xbox");
        if (body) {
            request.Content(http::HttpStringContent(
                winrt::to_hstring(body),
                winrt::Windows::Storage::Streams::UnicodeEncoding::Utf8,
                L"application/json"));
        }
        for (auto h = headers; h && *h; ++h) {
            const std::string line(*h);
            const auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            auto value = line.substr(colon + 1);
            while (!value.empty() && value.front() == ' ') value.erase(value.begin());
            request.Headers().TryAppendWithoutValidation(
                winrt::to_hstring(line.substr(0, colon)), winrt::to_hstring(value));
        }
        const auto pending = client.SendRequestAsync(request);
        if (pending.wait_for(std::chrono::seconds(12)) == winrt::Windows::Foundation::AsyncStatus::Started) {
            pending.Cancel();
            return 0;
        }
        const auto response = pending.get();
        const auto contents = winrt::to_string(response.Content().ReadAsStringAsync().get());
        if (contents.empty()) return 0;
        const auto length = contents.size() < capacity - 1 ? contents.size() : capacity - 1;
        std::memcpy(answer, contents.data(), length);
        answer[length] = '\0';
        return 1;
    } catch (const winrt::hresult_error &) {
        return 0;
    } catch (...) {
        return 0;
    }
}
