#include "catch.hpp"

#include "util/uri.h"

using namespace Tangram;

TEST_CASE("Parse components of a correctly formatted URL", "[Uri]") {

    Uri uri("https://vector.mapzen.com:8080/osm/all/0/0/0.mvt?api_key=mapsRcool#yolo");

    CHECK(uri.hasScheme());
    CHECK(uri.scheme() == "https");
    CHECK(uri.hasHost());
    CHECK(uri.host() == "vector.mapzen.com");
    CHECK(uri.hasPort());
    CHECK(uri.port() == "8080");
    CHECK(uri.portNumber() == 8080);
    CHECK(uri.hasPath());
    CHECK(uri.path() == "/osm/all/0/0/0.mvt");
    CHECK(uri.hasQuery());
    CHECK(uri.query() == "api_key=mapsRcool");
    CHECK(uri.hasFragment());
    CHECK(uri.fragment() == "yolo");

}
