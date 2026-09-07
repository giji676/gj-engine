#pragma once

#include <typeinfo>
#include <vector>

#include "engine/entity.h"
#include "engine/object_ref.h"

enum class InspectType {
    Bool,
    Int,
    Float,
    Object,
    Component,
    Script,
};

struct InspectField {
    const char* name = "";
    InspectType type = InspectType::Bool;
    void* ptr = nullptr;
    const std::type_info* requiredType = nullptr;
};

class Inspectable {
public:
    const std::vector<InspectField>& inspectFields() const { return fields_; }

protected:
    void expose(const char* name, bool& value) {
        add(name, InspectType::Bool, &value);
    }
    void expose(const char* name, int& value) {
        add(name, InspectType::Int, &value);
    }
    void expose(const char* name, float& value) {
        add(name, InspectType::Float, &value);
    }
    void expose(const char* name, EntityRef& value) {
        add(name, InspectType::Object, &value.id);
    }
    void expose(const char* name, Entity& value) {
        add(name, InspectType::Object, &value);
    }

    template <typename T>
    void expose(const char* name, ComponentRef<T>& value) {
        add(name, InspectType::Component, &value.id, &typeid(T));
    }

    template <typename T>
    void expose(const char* name, ScriptRef<T>& value) {
        add(name, InspectType::Script, &value.id, &typeid(T));
    }

private:
    std::vector<InspectField> fields_;

    void add(
            const char* name,
            InspectType type,
            void* ptr,
            const std::type_info* requiredType = nullptr) {
        fields_.push_back({name, type, ptr, requiredType});
    }
};

#define INSPECT(member) expose(#member, member)
