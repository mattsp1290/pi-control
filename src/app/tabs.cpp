#include "app/tabs.h"

#include "app/example_page.h"
#include "app/home_page.h"
#include "app/settings_page.h"

namespace pi_control::app {

const std::vector<TabDescriptor>& tabs() {
    static const std::vector<TabDescriptor> list{
        {"home", "Home", [](const AppContext& context) { return new HomePage(context); }},
        {"example", "Example", [](const AppContext&) { return new ExamplePage; }},
        {"settings", "Settings", [](const AppContext& context) { return new SettingsPage(context); }},
    };
    return list;
}

} // namespace pi_control::app
