ut_dep = dependency('boost.ut', fallback: ['ut', 'boostut_dep'])

test_exe = executable(
    'test_cli',
    'test_cli.cpp',
    dependencies: [cli23_dep, ut_dep],
)

test('cli23', test_exe)
