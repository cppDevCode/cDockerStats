// container_object.h – ContainerObject wrapper for GListStore

#ifndef CONTAINER_OBJECT_H
#define CONTAINER_OBJECT_H

#include <glib-object.h>
#include "core/docker_client.h"

#define CONTAINER_TYPE_OBJECT (container_object_get_type())
G_DECLARE_FINAL_TYPE(ContainerObject, container_object, CONTAINER, OBJECT, GObject)

struct _ContainerObject {
    GObject parent_instance;
    ContainerInfo *info;
};

ContainerObject *container_object_new(ContainerInfo *info);

#endif /* CONTAINER_OBJECT_H */
