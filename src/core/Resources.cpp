#include "core/Resources.h"

#include <QtCore/qglobal.h>

static void initResourcesOutsideNamespace()
{
    Q_INIT_RESOURCE(resources);
}

void ldInitResources()
{
    initResourcesOutsideNamespace();
}
