#include "common/status.h"
#include "test_framework.h"

using namespace vide;

int runStatusTests() {
    CHECK(Status::success().ok());
    CHECK(Status::success().code() == StatusCode::Ok);

    Status error = Status::error(StatusCode::IoError, "boom");
    CHECK(!error.ok());
    CHECK(error.code() == StatusCode::IoError);
    CHECK(error.message() == "boom");

    Result<int> value = 42;
    CHECK(value.ok());
    CHECK(value.value() == 42);
    CHECK(value.valueOr(0) == 42);

    Result<int> failure = Status::error(StatusCode::ParseError, "bad");
    CHECK(!failure.ok());
    CHECK(failure.valueOr(7) == 7);
    CHECK(failure.status().code() == StatusCode::ParseError);

    Result<void> ok = Ok();
    CHECK(ok.ok());
    Result<void> bad = Fail(StatusCode::Unsupported, "nope");
    CHECK(!bad.ok());

    return test::report("status");
}
