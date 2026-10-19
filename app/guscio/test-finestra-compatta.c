/* SPDX-License-Identifier: GPL-2.0-only */
/* Prova isolata della finestra compatta e del ciclo di vita del registro. */
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include "guscio.h"

static BOOL test_show_window(HWND window, int command)
{
    (void)window;
    (void)command;
    return 0;
}

#define ShowWindow test_show_window
#include "finestra.c"
#undef ShowWindow

static unsigned checks, failures, close_requests;

#define CHECK(x) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static void test_messaggi_guscio(UINT msg, WPARAM wp, LPARAM lp)
{
    (void)wp;
    (void)lp;
    if (msg == WM_APP + 1) {
        close_requests++;
    }
}

int main(void)
{
    Config config;
    HWND root, view, log_window;
    RECT client, view_client;
    CompatLayout expected;
    bool opened;
    UINT dpi;

    CHECK(_putenv_s("HABUMI_SINGLE_WINDOW", "1") == 0);
    CHECK(_putenv_s("GUSCIO_REGISTRO_FILE",
                    "registro-finestra-compatta.log") == 0);
    registro_apri();
    config_default(&config);
    CHECK(fin_titolo == NULL && fin_fasi == NULL && fin_log == NULL);

    opened = finestra_apri(&config);
    CHECK(opened);
    if (opened) {
        root = fin_hwnd;
        view = fin_vista;
        CHECK(root != NULL && IsWindow(root));
        CHECK(!IsWindowVisible(root));
        CHECK(view != NULL && IsWindow(view));
        CHECK(fin_more != NULL && IsWindow(fin_more));
        CHECK(fin_titolo == NULL && fin_fasi == NULL && fin_log == NULL);

        CHECK(GetClientRect(root, &client) != 0);
        CHECK(GetClientRect(view, &view_client) != 0);
        dpi = dpi_di_finestra(root);
        expected = compat_layout(client.right, client.bottom,
                                 dpi_scala(FIN_BARRA, dpi));
        CHECK(view_client.left == 0 && view_client.top == 0);
        CHECK(view_client.right == expected.view_width);
        CHECK(view_client.bottom == expected.view_height);
        CHECK(client.right - view_client.right == dpi_scala(36, dpi));

        finestra_messaggi_guscio(test_messaggi_guscio);
        fin_apri_log();
        CHECK(fin_log_finestra != NULL && IsWindow(fin_log_finestra));
        CHECK(fin_log != NULL && IsWindow(fin_log));
        CHECK(!IsWindowVisible(fin_log_finestra));
        log_window = fin_log_finestra;
        SendMessageA(log_window, WM_CLOSE, 0, 0);
        CHECK(!IsWindow(log_window));
        CHECK(fin_log_finestra == NULL && fin_log == NULL);
        CHECK(IsWindow(root) && IsWindow(view));
        CHECK(close_requests == 0);

        fin_apri_log();
        CHECK(fin_log_finestra != NULL && IsWindow(fin_log_finestra));
        CHECK(fin_log != NULL && IsWindow(fin_log));
        CHECK(!IsWindowVisible(fin_log_finestra));
        log_window = fin_log_finestra;
        CHECK(DestroyWindow(root) != 0);
        CHECK(!IsWindow(root) && !IsWindow(view) && !IsWindow(log_window));
        CHECK(fin_hwnd == NULL && fin_log_finestra == NULL && fin_log == NULL);
        CHECK(close_requests == 0);
        finestra_messaggi_guscio(NULL);
    } else if (fin_hwnd && IsWindow(fin_hwnd)) {
        DestroyWindow(fin_hwnd);
    }

    CHECK(sessione_ferma());
    registro_chiudi();
    _putenv_s("HABUMI_SINGLE_WINDOW", "");
    _putenv_s("GUSCIO_REGISTRO_FILE", "");
    printf("test-finestra-compatta: %u/%u checks passed\n",
           checks - failures, checks);
    return failures ? 1 : 0;
}
