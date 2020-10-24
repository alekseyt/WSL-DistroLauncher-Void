//
//    Copyright (C) Microsoft.  All rights reserved.
// Licensed under the terms described in the LICENSE file in the root of this project.
//

#include "stdafx.h"
#include <iostream>

bool DistributionInfo::CreateUser(std::wstring_view userName)
{
    const auto userNameStr = std::wstring(userName);

    // Create the user account.
    DWORD exitCode;
    const std::wstring createUserCommandLine = L"/usr/sbin/useradd -m " + userNameStr;
    HRESULT hr = g_wslApi.WslLaunchInteractive(createUserCommandLine.c_str(), true, &exitCode);
    if ((FAILED(hr)) || (exitCode != 0)) {
        return false;
    }

    const std::wstring mirrorURL = L"https://mirror.yandex.ru/mirrors/voidlinux";
    const std::wstring mirrorsDocURL = L"https://docs.voidlinux.org/xbps/repositories/mirrors/index.html";

    const std::wstring postUserCommands[] = {
        // Set user pasword, so `sudo` can be used
        // (but not `su` because there is no root password)
        L"/usr/sbin/passwd " + userNameStr,
        // Important group to set is `wheel`
        L"/usr/sbin/usermod -aG adm,wheel,floppy,cdrom,tape " + userNameStr,
        // Add wheel group to sudoers. `sudo` is already installed
        LR"(echo "%wheel ALL=(ALL) ALL" >/etc/sudoers.d/wheel)",
        // Regenerate locales so default one (`en_US.UTF-8`) is properly initialized
        L"/usr/sbin/xbps-reconfigure -f glibc-locales",
        // Setup mirror
        L"mkdir -p /etc/xbps.d",
        L"cp /usr/share/xbps.d/*-repository-*.conf /etc/xbps.d/",
        LR"(sed -i 's|https://alpha.de.repo.voidlinux.org|)" + mirrorURL + LR"(|g' /etc/xbps.d/*-repository-*.conf)",
    };

    for (const auto& postUserCommandLine : postUserCommands) {
        hr = g_wslApi.WslLaunchInteractive(postUserCommandLine.c_str(), true, &exitCode);
        if ((FAILED(hr)) || (exitCode != 0)) {
            // Delete the user if any command failed
            const std::wstring deleteUserCommandLine = L"/usr/sbin/userdel " + userNameStr;
            g_wslApi.WslLaunchInteractive(deleteUserCommandLine.c_str(), true, &exitCode);
            return false;
        }
    }

    std::wcout << std::endl
        << L"Your mirror is " + mirrorURL << std::endl
        << L"For more information on mirrors visit " + mirrorsDocURL << std::endl
        << std::endl;

    return true;
}

ULONG DistributionInfo::QueryUid(std::wstring_view userName)
{
    // Create a pipe to read the output of the launched process.
    HANDLE readPipe;
    HANDLE writePipe;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, true};
    ULONG uid = UID_INVALID;
    if (CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        // Query the UID of the supplied username.
        std::wstring command = L"/usr/bin/id -u ";
        command += userName;
        int returnValue = 0;
        HANDLE child;
        HRESULT hr = g_wslApi.WslLaunch(command.c_str(), true, GetStdHandle(STD_INPUT_HANDLE), writePipe, GetStdHandle(STD_ERROR_HANDLE), &child);
        if (SUCCEEDED(hr)) {
            // Wait for the child to exit and ensure process exited successfully.
            WaitForSingleObject(child, INFINITE);
            DWORD exitCode;
            if ((GetExitCodeProcess(child, &exitCode) == false) || (exitCode != 0)) {
                hr = E_INVALIDARG;
            }

            CloseHandle(child);
            if (SUCCEEDED(hr)) {
                char buffer[64];
                DWORD bytesRead;

                // Read the output of the command from the pipe and convert to a UID.
                if (ReadFile(readPipe, buffer, (sizeof(buffer) - 1), &bytesRead, nullptr)) {
                    buffer[bytesRead] = ANSI_NULL;
                    try {
                        uid = std::stoul(buffer, nullptr, 10);

                    } catch( ... ) { }
                }
            }
        }

        CloseHandle(readPipe);
        CloseHandle(writePipe);
    }

    return uid;
}
