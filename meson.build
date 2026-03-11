project(
    'cli23',
    'cpp',
    version: '0.1.0',
    license: 'MIT',
    meson_version: '>=1.1',
    default_options: [
        'cpp_std=c++23',
        'warning_level=3',
        'werror=false',
    ],
)

if build_machine.system() == 'darwin'
  add_project_arguments('-mmacosx-version-min=15.0', language: 'cpp')
endif

cxx = meson.get_compiler('cpp')

charconv_probe= '''
#include <charconv>
#include <string_view>
int main() {
double d{};
std::string_view s = "3.14";
std::from_chars(s.begin(), s.end(), d);
return 0;
}
'''

if cxx.compiles(charconv_probe, name: 'std::from_chars float')
  add_project_arguments('-DCLI23_HAS_FROM_CHARS', language: 'cpp')
endif


# Public dependency (header-only) 

cli23_inc = include_directories('include')

cli23_dep = declare_dependency(
    include_directories: cli23_inc,
)

# Tests 

if get_option('tests')
    subdir('tests')
endif
