#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "update_verify.hpp"
#include <bcrypt.h>
#include <vector>

namespace
{
    // Not every MinGW header has these.
    const wchar_t *const kSha256 = L"SHA256";
    const wchar_t *const kEcdsaP256 = L"ECDSA_P256";
    const wchar_t *const kEccPublicBlob = L"ECCPUBLICBLOB";

    bool Succeeded(NTSTATUS status)
    {
        return status >= 0;
    }
}

namespace update_verify
{
    Sha256::Sha256()
    {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        if (!Succeeded(BCryptOpenAlgorithmProvider(&algorithm, kSha256, nullptr, 0)))
            return;
        m_algorithm = algorithm;
        BCRYPT_HASH_HANDLE hash = nullptr;
        if (Succeeded(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0)))
        {
            m_hash = hash;
            m_ok = true;
        }
    }

    Sha256::~Sha256()
    {
        if (m_hash)
            BCryptDestroyHash(m_hash);
        if (m_algorithm)
            BCryptCloseAlgorithmProvider(m_algorithm, 0);
    }

    bool Sha256::Add(const void *data, size_t size)
    {
        if (!m_ok)
            return false;
        const unsigned char *bytes = static_cast<const unsigned char*>(data);
        while (size > 0)
        {
            ULONG piece = size > 0x100000 ? 0x100000 : static_cast<ULONG>(size);
            if (!Succeeded(BCryptHashData(m_hash, const_cast<unsigned char*>(bytes), piece, 0)))
            {
                m_ok = false;
                return false;
            }
            bytes += piece;
            size -= piece;
        }
        return true;
    }

    std::string Sha256::Finish()
    {
        unsigned char digest[32];
        if (!m_ok || !Succeeded(BCryptFinishHash(m_hash, digest, sizeof(digest), 0)))
            return std::string();
        m_ok = false;
        static const char kHex[] = "0123456789abcdef";
        std::string hex;
        for (unsigned char byte : digest)
        {
            hex += kHex[byte >> 4];
            hex += kHex[byte & 15];
        }
        return hex;
    }

    std::string HashFile(const std::wstring &path)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return std::string();
        Sha256 hash;
        std::vector<unsigned char> buffer(1 << 20);
        bool ok = true;
        for (;;)
        {
            DWORD read = 0;
            if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
            {
                ok = false;
                break;
            }
            if (read == 0)
                break;
            if (!hash.Add(buffer.data(), read))
            {
                ok = false;
                break;
            }
        }
        CloseHandle(file);
        return ok ? hash.Finish() : std::string();
    }

    bool VerifySignature(const unsigned char *publicKeyBlob, size_t blobSize, const void *data, size_t size,
        const unsigned char *signature, size_t signatureSize)
    {
        if (signatureSize != 64)
            return false;
        Sha256 hash;
        if (!hash.Add(data, size))
            return false;
        std::string hex = hash.Finish();
        if (hex.size() != 64)
            return false;
        unsigned char digest[32];
        for (size_t i = 0; i < 32; i++)
        {
            auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
            digest[i] = static_cast<unsigned char>(nibble(hex[2 * i]) << 4 | nibble(hex[2 * i + 1]));
        }

        BCRYPT_ALG_HANDLE algorithm = nullptr;
        if (!Succeeded(BCryptOpenAlgorithmProvider(&algorithm, kEcdsaP256, nullptr, 0)))
            return false;
        BCRYPT_KEY_HANDLE key = nullptr;
        bool ok = Succeeded(BCryptImportKeyPair(algorithm, nullptr, kEccPublicBlob, &key,
            const_cast<unsigned char*>(publicKeyBlob), static_cast<ULONG>(blobSize), 0))
            && Succeeded(BCryptVerifySignature(key, nullptr, digest, sizeof(digest),
                const_cast<unsigned char*>(signature), static_cast<ULONG>(signatureSize), 0));
        if (key)
            BCryptDestroyKey(key);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return ok;
    }
}
