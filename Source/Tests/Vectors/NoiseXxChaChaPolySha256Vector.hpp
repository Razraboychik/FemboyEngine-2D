// FemboyEngine2D - reference Noise XX vector for the conformance test.
//
// Source: rweather/noise-c, tests/vector/noise-c-basic.txt, the
// Noise_XX_25519_ChaChaPoly_SHA256 entry that carries a handshake_hash.
// Retrieved 2026-10-31 at commit cfe25410979a87391bb9ac8d4d4bef64e9f268c6;
// file SHA-256 0bd782283f414b711fbc37b8b5fbdde7816ce5328f1037d43742cf35dd84de49.
// The expected bytes below are copied verbatim. Never regenerate them from
// this engine: the noise-differential job rebuilds noise-c and does the diff.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Fby::Net::Conformance
{
inline constexpr char kVectorName[] = "Noise_XX_25519_ChaChaPoly_SHA256";
inline constexpr char kInitiatorStatic[] = "e61ef9919cde45dd5f82166404bd08e38bceb5dfdfded0a34c8df7ed542214d1";
inline constexpr char kInitiatorEphemeral[] = "893e28b9dc6ca8d611ab664754b8ceb7bac5117349a4439a6b0569da977c464a";
inline constexpr char kResponderStatic[] = "4a3acbfdb163dec651dfa3194dece676d437029c62a408b4c5ea9114246e4893";
inline constexpr char kResponderEphemeral[] = "bbdb4cdbd309f1a1f2e1456967fe288cadd6f712d65dc7b7793d5e63da6b375b";
inline constexpr char kInitiatorPrologue[] = "50726f6c6f677565313233";
inline constexpr char kResponderPrologue[] = "50726f6c6f677565313233";
inline constexpr char kHandshake1Payload[] = "4c756477696720766f6e204d69736573";
inline constexpr char kHandshake1[] = "ca35def5ae56cec33dc2036731ab14896bc4c75dbb07a61f879f8e3afa4c79444c756477696720766f6e204d69736573";
inline constexpr char kHandshake2Payload[] = "4d757272617920526f746862617264";
inline constexpr char kHandshake2[] = "95ebc60d2b1fa672c1f46a8aa265ef51bfe38e7ccb39ec5be34069f14480884381cbad1f276e038c48378ffce2b65285e08d6b68aaa3629a5a8639392490e5b94a6d8798832d5372f220f161d9c2df035528f8982ffe09be9b5c412f8a0db5d21351f20af1370d0bf8ef1a8c59a30e";
inline constexpr char kHandshake3Payload[] = "462e20412e20486179656b";
inline constexpr char kHandshake3[] = "c7195ffacac1307ff99046f219750fc47693e23c3cb08b89c2af808b444850a8589981dfbd651e6ff4724a781cc2aa6158c9fea0d4ec82a286427484c5b8c8123a7a6002b1de9f9775fc97";
inline constexpr char kTransport1Payload[] = "4361726c204d656e676572";
inline constexpr char kTransport1[] = "96763ed773f8e47bb3712f0e29b3060ffc956ffc146cee53d5e1df";
inline constexpr char kTransport2Payload[] = "4a65616e2d426170746973746520536179";
inline constexpr char kTransport2[] = "3e40f15f6f3a46ae446b253bf8b1d9ffb6ed9b174d272328ff91a7e2e5c79c07f5";
inline constexpr char kTransport3Payload[] = "457567656e2042f6686d20766f6e2042617765726b";
inline constexpr char kTransport3[] = "eb3f3515110702e047a6c9da4478b6ead94873c11c0f2d710ddb3f09fce024b3a58502ae3f";
inline constexpr char kHandshakeHash[] = "852a28d2146785c54bd8334f4e460c80d7fe4fd0cc5bc0abef2a24a3c4d44d5f";
}
