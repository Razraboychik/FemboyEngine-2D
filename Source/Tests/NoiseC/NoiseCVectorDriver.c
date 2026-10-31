// FemboyEngine2D - noise-c differential driver for the vendored XX vector.
//
// This program is built by the noise-differential CI job against the pinned
// noise-c checkout. It replays the vendored vector through noise-c and prints
// the three handshake messages, the handshake hash and the three transport
// ciphertexts. CompareNoiseCVector.py compares that output with the values the
// engine test uses; any difference fails the job.
#include <noise/protocol.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOISE_VECTOR_MAX_BYTES 512
#define NOISE_VECTOR_MESSAGE_BYTES 2048

static int Fail(const char* InWhat, int InError)
{
    char ScopedText[256];
    if (noise_strerror(InError, ScopedText, sizeof(ScopedText)) != 0)
    {
        strcpy(ScopedText, "unknown error");
    }
    fprintf(stderr, "noise_c_vector_driver: %s failed: %s (%d)\n", InWhat, ScopedText, InError);
    return 1;
}

static int HexNibble(char InCharacter)
{
    if (InCharacter >= '0' && InCharacter <= '9')
    {
        return InCharacter - '0';
    }
    if (InCharacter >= 'a' && InCharacter <= 'f')
    {
        return InCharacter - 'a' + 10;
    }
    if (InCharacter >= 'A' && InCharacter <= 'F')
    {
        return InCharacter - 'A' + 10;
    }
    return -1;
}

static size_t FromHex(const char* InText, unsigned char* OutBytes, size_t InMaxBytes)
{
    const size_t ScopedLength = strlen(InText);
    size_t ScopedIndex = 0;
    if ((ScopedLength & 1u) != 0 || ScopedLength / 2 > InMaxBytes)
    {
        return 0;
    }
    for (ScopedIndex = 0; ScopedIndex < ScopedLength / 2; ++ScopedIndex)
    {
        const int ScopedHigh = HexNibble(InText[ScopedIndex * 2]);
        const int ScopedLow = HexNibble(InText[ScopedIndex * 2 + 1]);
        if (ScopedHigh < 0 || ScopedLow < 0)
        {
            return 0;
        }
        OutBytes[ScopedIndex] = (unsigned char)((ScopedHigh << 4) | ScopedLow);
    }
    return ScopedIndex;
}

static void PrintHex(const char* InLabel, const unsigned char* InBytes, size_t InLength)
{
    size_t ScopedIndex = 0;
    printf("%s=", InLabel);
    for (ScopedIndex = 0; ScopedIndex < InLength; ++ScopedIndex)
    {
        printf("%02x", InBytes[ScopedIndex]);
    }
    printf("\n");
}

static unsigned char GInitStatic[NOISE_VECTOR_MAX_BYTES];
static unsigned char GInitEphemeral[NOISE_VECTOR_MAX_BYTES];
static unsigned char GRespStatic[NOISE_VECTOR_MAX_BYTES];
static unsigned char GRespEphemeral[NOISE_VECTOR_MAX_BYTES];
static unsigned char GPrologue[NOISE_VECTOR_MAX_BYTES];
static unsigned char GPayload[6][NOISE_VECTOR_MAX_BYTES];
static size_t GInitStaticLength = 0;
static size_t GInitEphemeralLength = 0;
static size_t GRespStaticLength = 0;
static size_t GRespEphemeralLength = 0;
static size_t GPrologueLength = 0;
static size_t GPayloadLength[6] = {0, 0, 0, 0, 0, 0};

static int LoadVector(const char* InPath)
{
    FILE* ScopedFile = fopen(InPath, "r");
    char ScopedLine[4 * NOISE_VECTOR_MAX_BYTES];
    if (ScopedFile == NULL)
    {
        fprintf(stderr, "noise_c_vector_driver: cannot open %s\n", InPath);
        return 0;
    }
    while (fgets(ScopedLine, sizeof(ScopedLine), ScopedFile) != NULL)
    {
        char* ScopedEquals = strchr(ScopedLine, '=');
        char* ScopedValue = NULL;
        size_t ScopedValueLength = 0;
        if (ScopedEquals == NULL)
        {
            continue;
        }
        *ScopedEquals = '\0';
        ScopedValue = ScopedEquals + 1;
        while (*ScopedValue == ' ' || *ScopedValue == '\t')
        {
            ++ScopedValue;
        }
        ScopedValueLength = strlen(ScopedValue);
        while (ScopedValueLength > 0 &&
               (ScopedValue[ScopedValueLength - 1] == '\n' || ScopedValue[ScopedValueLength - 1] == '\r'))
        {
            ScopedValue[--ScopedValueLength] = '\0';
        }
        if (strcmp(ScopedLine, "init_static") == 0)
        {
            GInitStaticLength = FromHex(ScopedValue, GInitStatic, sizeof(GInitStatic));
        }
        else if (strcmp(ScopedLine, "init_ephemeral") == 0)
        {
            GInitEphemeralLength = FromHex(ScopedValue, GInitEphemeral, sizeof(GInitEphemeral));
        }
        else if (strcmp(ScopedLine, "resp_static") == 0)
        {
            GRespStaticLength = FromHex(ScopedValue, GRespStatic, sizeof(GRespStatic));
        }
        else if (strcmp(ScopedLine, "resp_ephemeral") == 0)
        {
            GRespEphemeralLength = FromHex(ScopedValue, GRespEphemeral, sizeof(GRespEphemeral));
        }
        else if (strcmp(ScopedLine, "prologue") == 0)
        {
            GPrologueLength = FromHex(ScopedValue, GPrologue, sizeof(GPrologue));
        }
        else if (strncmp(ScopedLine, "payload", 7) == 0 && ScopedLine[7] >= '1' && ScopedLine[7] <= '6')
        {
            const size_t ScopedIndex = (size_t)(ScopedLine[7] - '1');
            GPayloadLength[ScopedIndex] = FromHex(ScopedValue, GPayload[ScopedIndex], NOISE_VECTOR_MAX_BYTES);
        }
    }
    fclose(ScopedFile);
    return GInitStaticLength == 32 && GInitEphemeralLength == 32 && GRespStaticLength == 32 &&
           GRespEphemeralLength == 32 && GPrologueLength > 0 && GPayloadLength[0] > 0 && GPayloadLength[1] > 0 &&
           GPayloadLength[2] > 0 && GPayloadLength[3] > 0 && GPayloadLength[4] > 0 && GPayloadLength[5] > 0;
}

static int SetLocalStatic(NoiseHandshakeState* InState, const unsigned char* InPrivate, size_t InLength)
{
    NoiseDHState* ScopedDh = noise_handshakestate_get_local_keypair_dh(InState);
    if (ScopedDh == NULL)
    {
        return NOISE_ERROR_INVALID_STATE;
    }
    return noise_dhstate_set_keypair_private(ScopedDh, InPrivate, InLength);
}

static int SetFixedEphemeral(NoiseHandshakeState* InState, const unsigned char* InPrivate, size_t InLength)
{
    NoiseDHState* ScopedDh = noise_handshakestate_get_fixed_ephemeral_dh(InState);
    if (ScopedDh == NULL)
    {
        return NOISE_ERROR_INVALID_STATE;
    }
    return noise_dhstate_set_keypair_private(ScopedDh, InPrivate, InLength);
}

static int WriteMessage(NoiseHandshakeState* InState, const unsigned char* InPayload, size_t InPayloadLength,
                        unsigned char* OutMessage, size_t* OutLength)
{
    NoiseBuffer ScopedMessageBuffer;
    NoiseBuffer ScopedPayloadBuffer;
    int ScopedError = NOISE_ERROR_NONE;
    noise_buffer_set_output(ScopedMessageBuffer, OutMessage, NOISE_VECTOR_MESSAGE_BYTES);
    noise_buffer_set_input(ScopedPayloadBuffer, InPayload, InPayloadLength);
    ScopedError = noise_handshakestate_write_message(InState, &ScopedMessageBuffer, &ScopedPayloadBuffer);
    if (ScopedError == NOISE_ERROR_NONE)
    {
        *OutLength = ScopedMessageBuffer.size;
    }
    return ScopedError;
}

static int ReadMessage(NoiseHandshakeState* InState, const unsigned char* InMessage, size_t InLength,
                       unsigned char* OutPayload, size_t* OutPayloadLength)
{
    NoiseBuffer ScopedMessageBuffer;
    NoiseBuffer ScopedPayloadBuffer;
    int ScopedError = NOISE_ERROR_NONE;
    noise_buffer_set_input(ScopedMessageBuffer, InMessage, InLength);
    noise_buffer_set_output(ScopedPayloadBuffer, OutPayload, NOISE_VECTOR_MAX_BYTES);
    ScopedError = noise_handshakestate_read_message(InState, &ScopedMessageBuffer, &ScopedPayloadBuffer);
    if (ScopedError == NOISE_ERROR_NONE)
    {
        *OutPayloadLength = ScopedPayloadBuffer.size;
    }
    return ScopedError;
}

int main(int InArgc, char** InArgv)
{
    NoiseHandshakeState* ScopedInitiator = NULL;
    NoiseHandshakeState* ScopedResponder = NULL;
    NoiseCipherState* ScopedInitiatorSend = NULL;
    NoiseCipherState* ScopedInitiatorReceive = NULL;
    NoiseCipherState* ScopedResponderSend = NULL;
    NoiseCipherState* ScopedResponderReceive = NULL;
    unsigned char ScopedMessage[NOISE_VECTOR_MESSAGE_BYTES];
    unsigned char ScopedReadPayload[NOISE_VECTOR_MAX_BYTES];
    unsigned char ScopedHash[64];
    NoiseBuffer ScopedBuffer;
    size_t ScopedMessageLength = 0;
    size_t ScopedPayloadLength = 0;
    int ScopedError = NOISE_ERROR_NONE;

    if (InArgc != 2 || !LoadVector(InArgv[1]))
    {
        fprintf(stderr, "usage: noise_c_vector_driver <vector-input.txt>\n");
        return 2;
    }

    ScopedError = noise_handshakestate_new_by_name(&ScopedInitiator, "Noise_XX_25519_ChaChaPoly_SHA256",
                                                   NOISE_ROLE_INITIATOR);
    if (ScopedError != NOISE_ERROR_NONE)
    {
        return Fail("initiator", ScopedError);
    }
    ScopedError = noise_handshakestate_new_by_name(&ScopedResponder, "Noise_XX_25519_ChaChaPoly_SHA256",
                                                   NOISE_ROLE_RESPONDER);
    if (ScopedError != NOISE_ERROR_NONE)
    {
        return Fail("responder", ScopedError);
    }

    ScopedError = SetLocalStatic(ScopedInitiator, GInitStatic, GInitStaticLength);
    if (ScopedError != NOISE_ERROR_NONE)
    {
        return Fail("initiator static key", ScopedError);
    }
    ScopedError = SetLocalStatic(ScopedResponder, GRespStatic, GRespStaticLength);
    if (ScopedError != NOISE_ERROR_NONE)
    {
        return Fail("responder static key", ScopedError);
    }
    ScopedError = SetFixedEphemeral(ScopedInitiator, GInitEphemeral, GInitEphemeralLength);
    if (ScopedError != NOISE_ERROR_NONE)
    {
        return Fail("initiator ephemeral key", ScopedError);
    }
    ScopedError = SetFixedEphemeral(ScopedResponder, GRespEphemeral, GRespEphemeralLength);
    if (ScopedError != NOISE_ERROR_NONE)
    {
        return Fail("responder ephemeral key", ScopedError);
    }
    ScopedError = noise_handshakestate_set_prologue(ScopedInitiator, GPrologue, GPrologueLength);
    if (ScopedError != NOISE_ERROR_NONE)
    {
        return Fail("initiator prologue", ScopedError);
    }
    ScopedError = noise_handshakestate_set_prologue(ScopedResponder, GPrologue, GPrologueLength);
    if (ScopedError != NOISE_ERROR_NONE)
    {
        return Fail("responder prologue", ScopedError);
    }
    ScopedError = noise_handshakestate_start(ScopedInitiator);
    if (ScopedError != NOISE_ERROR_NONE)
    {
        return Fail("initiator start", ScopedError);
    }
    ScopedError = noise_handshakestate_start(ScopedResponder);
    if (ScopedError != NOISE_ERROR_NONE)
    {
        return Fail("responder start", ScopedError);
    }

    // Message 1: the initiator writes and the responder reads.
    ScopedError = WriteMessage(ScopedInitiator, GPayload[0], GPayloadLength[0], ScopedMessage, &ScopedMessageLength);
    if (ScopedError != NOISE_ERROR_NONE)
    {
        return Fail("message 1 write", ScopedError);
    }
    PrintHex("msg1", ScopedMessage, ScopedMessageLength);
    ScopedError = ReadMessage(ScopedResponder, ScopedMessage, ScopedMessageLength, ScopedReadPayload,
                              &ScopedPayloadLength);
    if (ScopedError != NOISE_ERROR_NONE)
    {
        return Fail("message 1 read", ScopedError);
    }
    PrintHex("payload1", ScopedReadPayload, ScopedPayloadLength);

    // Message 2: the responder writes and the initiator reads.
    ScopedError = WriteMessage(ScopedResponder, GPayload[1], GPayloadLength[1], ScopedMessage, &ScopedMessageLength);
    if (ScopedError != NOISE_ERROR_NONE)
    {
        return Fail("message 2 write", ScopedError);
    }
    PrintHex("msg2", ScopedMessage, ScopedMessageLength);
    ScopedError = ReadMessage(ScopedInitiator, ScopedMessage, ScopedMessageLength, ScopedReadPayload,
                              &ScopedPayloadLength);
    if (ScopedError != NOISE_ERROR_NONE)
    {
        return Fail("message 2 read", ScopedError);
    }
    PrintHex("payload2", ScopedReadPayload, ScopedPayloadLength);

    // Message 3: the initiator writes and the responder reads.
    ScopedError = WriteMessage(ScopedInitiator, GPayload[2], GPayloadLength[2], ScopedMessage, &ScopedMessageLength);
    if (ScopedError != NOISE_ERROR_NONE)
    {
        return Fail("message 3 write", ScopedError);
    }
    PrintHex("msg3", ScopedMessage, ScopedMessageLength);
    ScopedError = ReadMessage(ScopedResponder, ScopedMessage, ScopedMessageLength, ScopedReadPayload,
                              &ScopedPayloadLength);
    if (ScopedError != NOISE_ERROR_NONE)
    {
        return Fail("message 3 read", ScopedError);
    }
    PrintHex("payload3", ScopedReadPayload, ScopedPayloadLength);

    ScopedError = noise_handshakestate_get_handshake_hash(ScopedInitiator, ScopedHash, 32);
    if (ScopedError != NOISE_ERROR_NONE)
    {
        return Fail("handshake hash", ScopedError);
    }
    PrintHex("hash", ScopedHash, 32);

    ScopedError = noise_handshakestate_split(ScopedInitiator, &ScopedInitiatorSend, &ScopedInitiatorReceive);
    if (ScopedError != NOISE_ERROR_NONE)
    {
        return Fail("initiator split", ScopedError);
    }
    ScopedError = noise_handshakestate_split(ScopedResponder, &ScopedResponderSend, &ScopedResponderReceive);
    if (ScopedError != NOISE_ERROR_NONE)
    {
        return Fail("responder split", ScopedError);
    }

    // The reference transport list starts with the responder's turn.
    memcpy(ScopedMessage, GPayload[3], GPayloadLength[3]);
    noise_buffer_set_inout(ScopedBuffer, ScopedMessage, GPayloadLength[3], sizeof(ScopedMessage));
    ScopedError = noise_cipherstate_encrypt(ScopedResponderSend, &ScopedBuffer);
    if (ScopedError != NOISE_ERROR_NONE)
    {
        return Fail("transport 1", ScopedError);
    }
    PrintHex("t1", ScopedMessage, ScopedBuffer.size);

    memcpy(ScopedMessage, GPayload[4], GPayloadLength[4]);
    noise_buffer_set_inout(ScopedBuffer, ScopedMessage, GPayloadLength[4], sizeof(ScopedMessage));
    ScopedError = noise_cipherstate_encrypt(ScopedInitiatorSend, &ScopedBuffer);
    if (ScopedError != NOISE_ERROR_NONE)
    {
        return Fail("transport 2", ScopedError);
    }
    PrintHex("t2", ScopedMessage, ScopedBuffer.size);

    memcpy(ScopedMessage, GPayload[5], GPayloadLength[5]);
    noise_buffer_set_inout(ScopedBuffer, ScopedMessage, GPayloadLength[5], sizeof(ScopedMessage));
    ScopedError = noise_cipherstate_encrypt(ScopedResponderSend, &ScopedBuffer);
    if (ScopedError != NOISE_ERROR_NONE)
    {
        return Fail("transport 3", ScopedError);
    }
    PrintHex("t3", ScopedMessage, ScopedBuffer.size);

    noise_cipherstate_free(ScopedInitiatorSend);
    noise_cipherstate_free(ScopedInitiatorReceive);
    noise_cipherstate_free(ScopedResponderSend);
    noise_cipherstate_free(ScopedResponderReceive);
    noise_handshakestate_free(ScopedInitiator);
    noise_handshakestate_free(ScopedResponder);
    return 0;
}
