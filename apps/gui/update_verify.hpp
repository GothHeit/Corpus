#ifndef UPDATE_VERIFY_HPP
#define UPDATE_VERIFY_HPP

#include <windows.h>
#include <cstddef>
#include <string>

// SHA-256 and ECDSA P-256 signature checks with Windows' own BCrypt, for the updater: nothing else is linked in.
namespace update_verify
{
    // A SHA-256 being fed piece by piece (a download as it arrives).
    class Sha256
    {
    public:
        Sha256();
        ~Sha256();
        Sha256(const Sha256 &) = delete;
        Sha256 &operator=(const Sha256 &) = delete;

        bool Add(const void *data, size_t size);
        // The digest as 64 lower-case hex digits; empty on failure. The object is spent afterwards.
        std::string Finish();

    private:
        void *m_algorithm = nullptr;
        void *m_hash = nullptr;
        bool m_ok = false;
    };

    // The SHA-256 of a file as 64 lower-case hex digits; empty if it can't be read.
    std::string HashFile(const std::wstring &path);

    // Whether 'signature' (raw r then s, 64 bytes) is the ECDSA P-256 signature over the SHA-256 of 'data' made by the key
    // in 'publicKeyBlob' (a BCRYPT_ECCPUBLIC_BLOB, 72 bytes).
    bool VerifySignature(const unsigned char *publicKeyBlob, size_t blobSize, const void *data, size_t size,
        const unsigned char *signature, size_t signatureSize);
}

#endif
