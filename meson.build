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

# Public dependency (header-only) 

cli23_inc = include_directories('include')

cli23_dep = declare_dependency(
    include_directories: cli23_inc,
)

# Tests 

if get_option('tests')
    subdir('tests')
endif
