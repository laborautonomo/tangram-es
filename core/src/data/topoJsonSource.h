#pragma once

#include "dataSource.h"

namespace Tangram {

class TopoJsonSource : public DataSource {

public:
    using DataSource::DataSource;

protected:

    virtual std::shared_ptr<TileData> parse(const TileTask& _task,
                                            const MapProjection& _projection) const override;

    // TODO: We need to register this MIME Media Type with the IANA
    virtual const char* mimeType() override { return "application/topo+json"; };
};

}
