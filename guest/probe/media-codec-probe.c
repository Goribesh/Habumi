/* SPDX-License-Identifier: GPL-2.0-only */
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    const char *mime = "video/avc";
    AMediaCodec *codec = NULL;
    AMediaFormat *format = NULL;
    char *codec_name = NULL;
    int exit_status = 1;
    int codec_started = 0;

    printf("probe=media_codec\n");
    printf("mime=%s\n", mime);
    printf("width=64\nheight=64\nsurface=false\ninput_data_supplied=false\n");

    codec = AMediaCodec_createDecoderByType(mime);
    if (codec == NULL) {
        fprintf(stderr, "decoder_creation=failed\n");
        exit_status = 2;
        goto cleanup;
    }
    printf("decoder_creation=ok\n");

    if (AMediaCodec_getName(codec, &codec_name) == AMEDIA_OK && codec_name != NULL) {
        printf("decoder_name=%s\n", codec_name);
        AMediaCodec_releaseName(codec, codec_name);
        codec_name = NULL;
    } else {
        printf("decoder_name=unavailable\n");
    }

    format = AMediaFormat_new();
    if (format == NULL) {
        fprintf(stderr, "format_creation=failed\n");
        exit_status = 3;
        goto cleanup;
    }
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, mime);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, 64);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, 64);

    media_status_t configure_status = AMediaCodec_configure(codec, format, NULL, NULL, 0);
    if (configure_status != AMEDIA_OK) {
        fprintf(stderr, "codec_configuration=failed status=%d\n", (int)configure_status);
        exit_status = 4;
        goto cleanup;
    }
    printf("codec_configuration=ok\n");

    media_status_t start_status = AMediaCodec_start(codec);
    if (start_status != AMEDIA_OK) {
        fprintf(stderr, "codec_start=failed status=%d\n", (int)start_status);
        exit_status = 5;
        goto cleanup;
    }
    codec_started = 1;
    printf("codec_start=ok\n");
    printf("decode_performed=false\n");
    exit_status = 0;

cleanup:
    if (codec_name != NULL && codec != NULL) {
        AMediaCodec_releaseName(codec, codec_name);
    }
    if (format != NULL) {
        AMediaFormat_delete(format);
    }
    if (codec != NULL) {
        if (codec_started) {
            media_status_t stop_status = AMediaCodec_stop(codec);
            printf("codec_stop=%s status=%d\n",
                   stop_status == AMEDIA_OK ? "ok" : "failed", (int)stop_status);
            if (stop_status != AMEDIA_OK && exit_status == 0) exit_status = 6;
        }
        media_status_t delete_status = AMediaCodec_delete(codec);
        printf("codec_delete=%s status=%d\n",
               delete_status == AMEDIA_OK ? "ok" : "failed", (int)delete_status);
        if (delete_status != AMEDIA_OK && exit_status == 0) exit_status = 7;
    }
    return exit_status;
}
