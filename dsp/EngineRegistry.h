/*  EngineRegistry — the single place a new filter gets added.

    APPEND ONLY. The integer index of an engine is written into host sessions via
    the slot's engine choice parameter, and the string id is written into the
    plugin's own state, so reordering this table would silently change existing
    users' patches. Adding at the end is always safe.
*/
#pragma once

#include "ToposCore.h"
#include "engines/ScatterModal.h"
#include "engines/EnergyRoute.h"
#include "engines/NonlinDisperse.h"
#include "engines/HystereticFormant.h"
#include "engines/ElectrothermalLadder.h"

namespace topos {

/** index 0: pass-through, so an empty slot costs nothing */
class NoneEngine final : public FilterEngine
{
public:
    static const EngineDescriptor& descriptor()
    {
        static const EngineDescriptor d = { "none", "— empty —", "Slot bypassed", 0, {} };
        return d;
    }
    const EngineDescriptor& desc() const override { return descriptor(); }
    void prepare (double sampleRate, int) override { sr = sampleRate; }
    void reset() override {}
    void setParams (const float*) override {}
    void process (float*, float*, int) override {}
};

struct EngineEntry
{
    const EngineDescriptor* (*describe)();
    FilterEngine*           (*create)();
};

template <typename T>
static const EngineDescriptor* describeT() { return &T::descriptor(); }
template <typename T>
static FilterEngine* createT() { return new T(); }

class EngineRegistry
{
public:
    static const EngineEntry* table()
    {
        static const EngineEntry t[] = {
            { &describeT<NoneEngine>,             &createT<NoneEngine> },
            { &describeT<ScatterModal>,           &createT<ScatterModal> },
            { &describeT<EnergyRoute>,            &createT<EnergyRoute> },
            { &describeT<NonlinDisperse>,         &createT<NonlinDisperse> },
            { &describeT<HystereticFormant>,      &createT<HystereticFormant> },
            { &describeT<ElectrothermalLadder>,   &createT<ElectrothermalLadder> },
            // <-- append new engines here, never above this line
        };
        return t;
    }

    static int count()
    {
        return 6;   // keep in sync with the table above
    }

    static const EngineDescriptor& descriptor (int index)
    {
        const int i = (index < 0 || index >= count()) ? 0 : index;
        return *table()[i].describe();
    }

    static std::unique_ptr<FilterEngine> create (int index)
    {
        const int i = (index < 0 || index >= count()) ? 0 : index;
        return std::unique_ptr<FilterEngine> (table()[i].create());
    }

    static int indexOf (const char* id)
    {
        for (int i = 0; i < count(); ++i)
            if (std::strcmp (descriptor (i).id, id) == 0) return i;
        return 0;
    }
};

} // namespace topos
