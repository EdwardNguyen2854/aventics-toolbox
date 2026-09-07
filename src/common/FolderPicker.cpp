#include <ProToolkit.h>
#include "common/FolderPicker.h"

#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>

bool FolderPicker::Pick(const std::wstring& title, const std::wstring& initialFolder, std::wstring& selectedFolder) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool mustUninit = SUCCEEDED(init);

    IFileDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog) {
        if (mustUninit) CoUninitialize();
        return false;
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(title.c_str());

    IShellItem* initial = nullptr;
    if (!initialFolder.empty() && SUCCEEDED(SHCreateItemFromParsingName(initialFolder.c_str(), nullptr, IID_PPV_ARGS(&initial)))) {
        dialog->SetFolder(initial);
        initial->Release();
    }

    bool ok = false;
    hr = dialog->Show(nullptr);
    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                selectedFolder = path;
                CoTaskMemFree(path);
                ok = true;
            }
            item->Release();
        }
    }

    dialog->Release();
    if (mustUninit) CoUninitialize();
    return ok;
}
