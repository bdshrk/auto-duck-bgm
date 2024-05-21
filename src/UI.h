#pragma once

#include "resource.h"

#include "Engine.h"

static NOTIFYICONDATA nid;
static HWND hwnd;

static std::thread uiThread;

// the poll rate to check and process new messages in ms
static const float UI_POLL_RATE_MS = 50.0f;

// use for handling when quitting is requested from across multiple threads
static bool quitRequested = false;

int main(); // console entry point (debug)
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
                    _In_ LPWSTR lpCmdLine, _In_ int nShowCmd); // win entry
int run();

// window proc callback function
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

void createTrayIcon(HWND hwnd);
void createContextMenu(HWND hwnd);
void createErrorBox(std::wstring &errorString);

// function that handles creating ui and processing messages.
// should run on a separate thread.
void runUI();

// tells the ui AND ENGINE to quit asap. the entire program will terminate when
// both have honoured the request and quit.
void quit();
