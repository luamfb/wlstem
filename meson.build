project(
    'wlstem',
    'c',
    version: '0.1', #release_version
    license: 'MIT',
    meson_version: '>=0.53.0',
    default_options: [
        'c_std=c11',
        'warning_level=2',
        'werror=true',
    ],
)

add_project_arguments(
    [
        '-DWLR_USE_UNSTABLE',

        '-Wno-unused-parameter',
        '-Wno-unused-result',
        '-Wno-missing-braces',
        '-Wundef',
        '-Wvla',
    ],
    language: 'c',
)

cc = meson.get_compiler('c')

is_freebsd = host_machine.system().startswith('freebsd')
datadir = get_option('datadir')
sysconfdir = get_option('sysconfdir')
prefix = get_option('prefix')

if is_freebsd
    add_project_arguments('-D_C11_SOURCE', language: 'c')
endif

pcre           = dependency('libpcre')
wayland_server = dependency('wayland-server')
wayland_client = dependency('wayland-client')
wayland_cursor = dependency('wayland-cursor')
wayland_egl    = dependency('wayland-egl')
wayland_protos = dependency('wayland-protocols', version: '>=1.14')
xkbcommon      = dependency('xkbcommon')
cairo          = dependency('cairo')
pango          = dependency('pango')
pangocairo     = dependency('pangocairo')
pixman         = dependency('pixman-1')
glesv2         = dependency('glesv2')
libevdev       = dependency('libevdev')
libinput       = dependency('libinput', version: '>=1.6.0')
xcb            = dependency('xcb', required: get_option('xwayland'))
math           = cc.find_library('m')
rt             = cc.find_library('rt')

wlroots_version = ['>=0.12.0', '<0.13.0']
wlroots_features = {
    'xwayland': false,
    'systemd': false,
    'elogind': false,
    'libseat': false,
}
wlroots = dependency('wlroots', version: wlroots_version)
foreach name, _ : wlroots_features
    has = cc.get_define('WLR_HAS_' + name.to_upper(), prefix: '#include <wlr/config.h>', dependencies: wlroots) == '1'
    wlroots_features += { name: has }
endforeach

if get_option('xwayland').enabled() and not wlroots_features['xwayland']
    error('Cannot enable Xwayland in sway: wlroots has been built without Xwayland support')
endif
have_xwayland = xcb.found() and wlroots_features['xwayland']

if get_option('sd-bus-provider') == 'auto'
    sdbus = dependency('libsystemd',
        required: false,
        version: '>=239',
        not_found_message: 'libsystemd not found, trying libelogind',
    )
    if not sdbus.found()
        sdbus = dependency('libelogind',
            required: false,
            version: '>=239',
            not_found_message: 'libelogind not found, trying basu',
        )
    endif
    if not sdbus.found()
        sdbus = dependency('basu', required: false)
    endif
else
    sdbus = dependency(get_option('sd-bus-provider'))
endif

conf_data = configuration_data()

conf_data.set10('HAVE_XWAYLAND', have_xwayland)
conf_data.set10('HAVE_LIBSYSTEMD', sdbus.found() and sdbus.name() == 'libsystemd')
conf_data.set10('HAVE_LIBELOGIND', sdbus.found() and sdbus.name() == 'libelogind')
conf_data.set10('HAVE_BASU', sdbus.found() and sdbus.name() == 'basu')

add_project_arguments('-DSYSCONFDIR="/@0@"'.format(join_paths(prefix, sysconfdir)), language : 'c')

version = '"@0@"'.format(meson.project_version())
git = find_program('git', native: true, required: false)
if git.found()
    git_commit = run_command([git, 'rev-parse', '--short', 'HEAD'])
    git_branch = run_command([git, 'rev-parse', '--abbrev-ref', 'HEAD'])
    if git_commit.returncode() == 0 and git_branch.returncode() == 0
        version = '"@0@-@1@ (" __DATE__ ", branch \'@2@\')"'.format(
            meson.project_version(),
            git_commit.stdout().strip(),
            git_branch.stdout().strip(),
        )
    endif
endif
add_project_arguments('-DSWAY_VERSION=@0@'.format(version), language: 'c')

# Compute the relative path used by compiler invocations.
source_root = meson.current_source_dir().split('/')
build_root = meson.build_root().split('/')
relative_dir_parts = []
i = 0
in_prefix = true
foreach p : build_root
    if i >= source_root.length() or not in_prefix or p != source_root[i]
        in_prefix = false
        relative_dir_parts += '..'
    endif
    i += 1
endforeach
i = 0
in_prefix = true
foreach p : source_root
    if i >= build_root.length() or not in_prefix or build_root[i] != p
        in_prefix = false
        relative_dir_parts += p
    endif
    i += 1
endforeach
relative_dir = join_paths(relative_dir_parts) + '/'

# Strip relative path prefixes from the code if possible, otherwise hide them.
if cc.has_argument('-fmacro-prefix-map=/prefix/to/hide=')
    add_project_arguments(
        '-fmacro-prefix-map=@0@='.format(relative_dir),
        language: 'c',
    )
else
    add_project_arguments(
        '-DSWAY_REL_SRC_DIR="@0@"'.format(relative_dir),
        language: 'c',
    )
endif


sway_inc = include_directories('include', 'wlstem/include')

subdir('include')
subdir('protocols')
subdir('wlstem')
subdir('mod_sway')

summary({
    'xwayland': have_xwayland,
    'sd-bus': sdbus.found(),
}, bool_yn: true)

if not wlroots_features['systemd'] and not wlroots_features['elogind'] and not wlroots_features['libseat']
    warning('The sway binary must be setuid when compiled without (e)logind or libseat')
    warning('You must do this manually post-install: chmod a+s /path/to/sway')
endif
