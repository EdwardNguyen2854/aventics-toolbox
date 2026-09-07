#include <ProToolkit.h>
#include "common/ModelLoader.h"
#include "common/ModelUtils.h"

#include <ProMdl.h>

ProError ModelLoader::LoadPath(const std::wstring& path, ProMdlType type, ProMdl* model) {
    if (!model || path.empty()) return PRO_TK_BAD_INPUTS;
    ProPath proPath;
    if (!ModelUtils::CopyToProPath(path, proPath)) return PRO_TK_LINE_TOO_LONG;

    ProMdlfileType fileType = PRO_MDLFILE_UNUSED;
    if (type == PRO_MDL_PART) fileType = PRO_MDLFILE_PART;
    else if (type == PRO_MDL_ASSEMBLY) fileType = PRO_MDLFILE_ASSEMBLY;

    return ProMdlFiletypeLoad(proPath, fileType, PRO_B_FALSE, model);
}

ProError ModelLoader::Load(ModelDescriptor& descriptor, const SessionSnapshot& before) {
    if (descriptor.model) {
        descriptor.alreadyInSession = true;
        descriptor.loadedByToolbox = false;
        return PRO_TK_NO_ERROR;
    }
    if (descriptor.sourceType == ModelSourceType::Step) return PRO_TK_INVALID_TYPE;

    ProMdl mdl = nullptr;
    const ProError err = LoadPath(descriptor.sourcePath, descriptor.modelType, &mdl);
    if (err != PRO_TK_NO_ERROR) return err;

    descriptor.model = mdl;
    descriptor.alreadyInSession = before.Contains(mdl);
    descriptor.loadedByToolbox = !descriptor.alreadyInSession;
    return PRO_TK_NO_ERROR;
}
