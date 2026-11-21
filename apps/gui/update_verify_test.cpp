// The updater's checks on known values: SHA-256 of a string and a file, and a signature made by openssl (raw r||s)
// that must verify, and fail for a changed message, a changed signature or another key.
//
// Usage: corpus-update-verify-test

#include "update_verify.hpp"
#include "update_key.hpp"
#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;

static void check(bool ok, const char *what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        failures++;
    }
}

static const unsigned char kKey[72] = {
    0x45, 0x43, 0x53, 0x31, 0x20, 0x00, 0x00, 0x00, 0xbc, 0xef, 0xd7, 0xd3, 0x7f, 0xe7, 0xe1, 0x05,
    0xb2, 0xf0, 0x20, 0xb2, 0x33, 0x12, 0x6f, 0xbc, 0x2c, 0xed, 0xc4, 0xda, 0x4e, 0x5b, 0xf8, 0xe4,
    0xb9, 0xb2, 0xa6, 0x86, 0x65, 0x04, 0x1a, 0xf5, 0x7b, 0xe4, 0x34, 0xed, 0xe9, 0x11, 0x91, 0x35,
    0xf3, 0xe9, 0x26, 0xef, 0x0f, 0x50, 0x72, 0x7e, 0x6c, 0x17, 0xb2, 0x18, 0x6e, 0x76, 0x17, 0x5a,
    0xd2, 0xe4, 0x25, 0xa0, 0xaf, 0xda, 0x60, 0x40
};
static const unsigned char kSignature[64] = {
    0x99, 0xfd, 0x44, 0xed, 0xa1, 0x82, 0xcc, 0xa0, 0x78, 0x93, 0x06, 0x8c, 0xd7, 0x7e, 0x1a, 0x3d,
    0x71, 0xa4, 0x3f, 0xc9, 0xf3, 0x40, 0x3f, 0x0f, 0x63, 0x26, 0x4e, 0xd7, 0x4e, 0x0a, 0x86, 0x4a,
    0x20, 0x19, 0x8b, 0x57, 0x7d, 0x06, 0xc8, 0xa3, 0x71, 0x02, 0x36, 0xe5, 0xbc, 0x8b, 0x20, 0xff,
    0x35, 0x44, 0x98, 0x67, 0x75, 0x2b, 0x5d, 0x31, 0xa1, 0x2f, 0x9e, 0x91, 0x0d, 0x73, 0x6f, 0xeb
};

static const char kMessage[] = "abc 123\nSHA256SUMS test message\n";
static const char kMessageHash[] = "f4dccf6ba3f70f94a781b587c3aa22eb99dceb4392e5ca51f22f3737d465024c";

int wmain()
{
    const size_t length = sizeof(kMessage) - 1;
    {
        update_verify::Sha256 hash;
        check(hash.Add(kMessage, 8) && hash.Add(kMessage + 8, length - 8), "feed a hash in two pieces");
        check(hash.Finish() == kMessageHash, "SHA-256 of the message");
        update_verify::Sha256 empty;
        check(empty.Finish() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 of nothing");
    }
    {
        wchar_t path[MAX_PATH];
        GetTempPathW(MAX_PATH, path);
        std::wstring file = std::wstring(path) + L"corpus-update-verify-test.bin";
        FILE *f = _wfopen(file.c_str(), L"wb");
        check(f != nullptr, "write a temporary file");
        if (f)
        {
            fwrite(kMessage, 1, length, f);
            fclose(f);
            check(update_verify::HashFile(file) == kMessageHash, "SHA-256 of a file");
            DeleteFileW(file.c_str());
        }
        check(update_verify::HashFile(file).empty(), "SHA-256 of a missing file is empty");
    }

    check(update_verify::VerifySignature(kKey, sizeof(kKey), kMessage, length, kSignature, sizeof(kSignature)),
        "a good signature verifies");

    std::string changed = kMessage;
    changed[0] = 'A';
    check(!update_verify::VerifySignature(kKey, sizeof(kKey), changed.data(), changed.size(), kSignature, sizeof(kSignature)),
        "a changed message does not");
    unsigned char bad[64];
    std::memcpy(bad, kSignature, sizeof(bad));
    bad[40] ^= 1;
    check(!update_verify::VerifySignature(kKey, sizeof(kKey), kMessage, length, bad, sizeof(bad)), "a changed signature does not");
    check(!update_verify::VerifySignature(kKey, sizeof(kKey), kMessage, length, kSignature, 63), "a short signature does not");
    check(!update_verify::VerifySignature(kUpdatePublicKey, sizeof(kUpdatePublicKey), kMessage, length, kSignature, sizeof(kSignature)),
        "another key does not");
    unsigned char brokenKey[72];
    std::memcpy(brokenKey, kKey, sizeof(brokenKey));
    brokenKey[10] ^= 1;
    check(!update_verify::VerifySignature(brokenKey, sizeof(brokenKey), kMessage, length, kSignature, sizeof(kSignature)),
        "a key that is not on the curve does not");

    if (failures)
        return 1;
    std::printf("OK\n");
    return 0;
}
