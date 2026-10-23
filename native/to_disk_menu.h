#pragma once
#include <windows.h>
// Queue initial root hooks; activate only after the shared startup commit.
bool PrepareToDiskMenu(HMODULE cef);
void FinalizeToDiskMenu(bool applied);
void StartToDiskMenu(HMODULE cef);
