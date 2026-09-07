#pragma once

#include <ProToolkit.h>
#include <ProMdl.h>
#include <set>

class SessionSnapshot {
public:
    static SessionSnapshot Capture();
    bool Contains(ProMdl model) const;
    void CleanupNewModels(ProMdl preserve = nullptr) const;

private:
    std::set<ProMdl> models_;
};
