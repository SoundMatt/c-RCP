include(FetchContent)

# Unity — C unit testing framework (same framework c-FuSa's own test suite vendors)
FetchContent_Declare(
    Unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG        v2.6.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Unity)
