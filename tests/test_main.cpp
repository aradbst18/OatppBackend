#include "support/TestRunner.hpp"

#ifdef APP_WITH_POSTGRESQL
#include "support/TestDb.hpp"
#endif  // APP_WITH_POSTGRESQL

int main(int argc, char** argv)
{
    int result = app::tests::run(argc, argv);

#ifdef APP_WITH_POSTGRESQL
    if (!app::tests::pgTestDbRegistry().cleanup(std::cerr))
    {
        result = 1;
    }
#endif  // APP_WITH_POSTGRESQL

    return result;
}
