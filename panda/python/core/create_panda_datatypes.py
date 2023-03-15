#!/usr/bin/env python3
import re
import os
import sys
import shutil
if sys.version_info[0] < 3:
    raise RuntimeError('Requires python3')

# Autogenerate panda_datatypes.py and include/panda_datatypes.h
#
# Both of these files contain info in or derived from stuff in
# panda/include/panda.  Here, we autogenerate the two files so that we
# never have to worry about how to keep them in sync with the info in
# those include files.  See panda/include/panda/README.pypanda for
# so proscriptions wrt those headers we use here. They need to be kept
# fairly clean if we are to be able to make sense of them with this script
# which isn't terriby clever.

# Also copy all of the generated plog_pb2.py's into pandare/plog_pb/
# XXX: WIP if we do this this file should be renamed
root_dir = os.path.join(*[os.path.dirname(__file__), "..", "..", ".."]) # panda-git/ root dir
build_root = os.path.join(root_dir, "build")
lib_dir = os.path.join("pandare2", "data")

# for arch in ['arm', 'aarch64', 'i386', 'x86_64', 'ppc', 'mips', 'mipsel', 'mips64']:
#     softmmu = arch+"-softmmu"
#     plog = os.path.join(*[build_root, softmmu, "plog_pb2.py"])
#     if os.path.isfile(plog):
#         shutil.copy(plog, os.path.join(*["pandare", "plog_pb2.py"]))
#         break
# else:
#     raise RuntimeError("Unable to find any plog_pb2.py files in build directory")

OUTPUT_DIR = os.path.abspath(os.path.join(*[os.path.dirname(__file__), "pandare2", "autogen"]))                # panda-git/panda/python/core/pandare/autogen
PLUGINS_DIR = os.path.abspath(os.path.join(*[os.path.dirname(__file__), "..", "..", "plugins"]))              # panda-git/panda/plugins
INCLUDE_DIR_PYP = os.path.abspath(os.path.join(*[os.path.dirname(__file__), "pandare2", "include"]))           # panda-git/panda/python/core/pandare/include
INCLUDE_DIR_OVERRIDES = os.path.abspath(os.path.join(*[os.path.dirname(__file__), "pandare2", "include", "overrides"]))           # panda-git/panda/python/core/pandare/include
INCLUDE_DIR_OVERRIDES_QEMU = os.path.abspath(os.path.join(*[os.path.dirname(__file__), "pandare2", "include", "overrides", "qemu"]))           # panda-git/panda/python/core/pandare/include
INCLUDE_DIR_PAN = os.path.abspath(os.path.join(*[os.path.dirname(__file__), "..", "..", "include", "panda"])) # panda-git/panda/include/panda
INCLUDE_DIR_CORE = os.path.abspath(os.path.join(*[os.path.dirname(__file__), "..", "..", "..", "include"]))   # panda-git/include
QEMU_INCLUDE_DIR = os.path.abspath(os.path.join(root_dir,"include"))

GLOBAL_MAX_SYSCALL_ARG_SIZE = 64
GLOBAL_MAX_SYSCALL_ARGS = 17


pypanda_start_pattern = """// BEGIN_PYPANDA_NEEDS_THIS -- do not delete this comment bc pypanda
// api autogen needs it.  And don't put any compiler directives
// between this and END_PYPANDA_NEEDS_THIS except includes of other
// files in this directory that contain subsections like this one.
"""

pypanda_end_pattern = "// END_PYPANDA_NEEDS_THIS -- do not delete this comment!\n"


pypanda_headers = []

def is_panda_aware(filename):
    contents = open(filename).read()
    if pypanda_start_pattern in contents:
        if not pypanda_end_pattern in contents:
            raise RuntimeError(f"PANDA aware file {filename} missing pypanda end pattern")
        return True
    return False

def trim_pypanda(contents):
    '''
    Trim data between pypanda_start_pattern/pypanda_end_pattern
    return None if patterns aren't found
    '''
    a = contents.find(pypanda_start_pattern)
    if a == -1: return None
    a += len(pypanda_start_pattern)
    b = contents.find(pypanda_end_pattern)
    if b == -1: return None
    recurse = None
    if len(contents[b+len(pypanda_end_pattern):]):
        recurse = trim_pypanda(contents[b+len(pypanda_end_pattern):])
    if recurse:
        return contents[a:b]+recurse
    else:
        return contents[a:b]

def ppp_cb_typedef_regex():
    return re.compile(r"PPP_CB_TYPEDEF\( *(void|bool|int) *, *([a-zA-Z0-9_-]+) *, *(.*)\).*")

def typedef(ret_type, name, args):
    return f"typedef {ret_type} (*{name}_t)({args});"

def copy_ppp_header(filename):
    # For the PPP-like headers we look for typedefs and then make the void ppp_add_cb(name)(name_t); functions
    # and the bool ppp_remove_cb(name)(name_t)
    # This probably won't support everything
    pypanda_h = os.path.join(INCLUDE_DIR_PYP, os.path.split(filename)[-1])
    print("Creating pypanda PPP header [%s] for [%s]" % (pypanda_h, filename))
    new_contents = [f"//Autogenerated PPP header from {filename}"]
    reg = ppp_cb_typedef_regex()
    contents = open(filename).read()
    subcontents = trim_pypanda(contents)
    for line in subcontents.split("\n"):
        # now add void ppp_add_cb_{cb_name}({cb_name}_t);
        m = reg.match(line)
        if m:
            ret_type = m.groups(1)[0]
            name = m.groups(1)[1]
            args = m.groups(1)[2]
            new_contents.append(typedef(ret_type, name, args))
            new_contents.append(f"void ppp_add_cb_{name}({name}_t);")
            new_contents.append(f"bool ppp_remove_cb_{name}({name}_t);")
            # void ppp_add_cb_{cb_name}(void (*)({cb_args}))
        elif "PPP_CB_TYPEDEF" in line:
            raise Exception(f"Failed to parse: {line}")
        else:
            new_contents.append(line.strip())
    with open(pypanda_h, "w") as outfile:
        outfile.write("\n".join(new_contents))

    return pypanda_h

def create_pypanda_header(filename, no_record=False):
    '''
    Given a file name, copy it into pypanda's includes directory
    along with all nested includes it contians
    if no_record is set, we don't save it into the pypanda_headers list
    so you'll have to manually include it
    '''
    contents = open(filename).read()
    subcontents = trim_pypanda(contents)
    if not subcontents: return
    # look for local includes
    rest = []
    (plugin_dir,fn) = os.path.split(filename)
    for line in subcontents.split("\n"):
        foo = re.search('\#include "(.*)"$', line)
        if foo:
            nested_inc = foo.groups()[0]
            print("Found nested include of %s" % nested_inc)
            create_pypanda_header("%s/%s" % (plugin_dir,nested_inc))
        else:
            rest.append(line)
    new_contents = "\n".join(rest)
    new_contents = new_contents.replace(" QEMU_NORETURN ", " ")
    foo = re.search("([^\/]+)\.h$", filename)
    assert (not (foo is None))
    pypanda_h = os.path.join(INCLUDE_DIR_PYP, foo.groups()[0])+".h"
    print("Creating pypanda header [%s] for [%s]" % (pypanda_h, filename))
    with open(pypanda_h, "w") as pyph:
        pyph.write(new_contents)
    if not no_record:
        pypanda_headers.append(pypanda_h)

def read_but_exclude_garbage(filename):
    nongarbage = []
    with open(filename) as thefile:
        for line in thefile:
            keep = True
            if re.search("^\s*\#", line): # Has preprocessor directive
                if not re.search("^\s*\#define [^_]", line): # Not a defines
                    keep = False
            if keep:
                nongarbage.append(line)
        return nongarbage

pn = None
def include_this(pdth, fn):
    global pn
    fn = os.path.join(INCLUDE_DIR_PAN, fn)
    shortpath= "/".join(fn.split("/")[-4:]) # Hardcoded 4, might be wrong
    pdth.write("\n\n// -----------------------------------\n")
    if is_panda_aware(fn):
        pdth.write("// Pull number %d from (panda-aware) %s\n" % (pn,shortpath))
        contents = open(fn).read()
        subcontents = trim_pypanda(contents)
        packed_re = re.compile(r'PACKED_STRUCT([a-zA-Z0-9_-]*)')
        if "PACKED_STRUCT" in subcontents: # Replace PACKED_STRUCT(foo) with foo. For kernelinfo.h
            for line in subcontents.split("\n"):
                if "PACKED_STRUCT" in line:
                    struct_name = re.search(r'PACKED_STRUCT\(([a-zA-Z0-9_-]*)\)', line).group(1)
                    line = line.replace(f"PACKED_STRUCT({struct_name})", f"struct {struct_name}")
                pdth.write(line+"\n")
        else:
                pdth.write(subcontents)
    else:
        pdth.write("// Pull number %d from %s\n" % (pn,shortpath))

        for line in read_but_exclude_garbage(fn):
            pdth.write(line)
    pn += 1

def compile(arch, bits, pypanda_headers, install, static_inc):
    #from ..ffi_importer import ffi
    from cffi import FFI
    ffi = FFI()

    ffi.set_source(f"panda_{arch}_{bits}", None)
    if install:
        import os
        include_dir = os.path.abspath(os.path.join(*[os.path.dirname(__file__),  "pandare", "include"]))
    else:
        include_dir = static_inc

    def define_messy_header(ffi, fname):
        from subprocess import check_output
        import os
        '''Convenience function to pull in headers from file in C'''
        #print("Pulling cdefs from ", fname)
        # CFFI can't handle externs, but sometimes we have to extern C (as opposed to
        glib_flags = check_output("pkg-config --cflags --libs glib-2.0".split()).decode().split()
        out = check_output([
            "cpp",
            "-DPYPANDA",
            "-I", INCLUDE_DIR_OVERRIDES,
            "-I", INCLUDE_DIR_OVERRIDES_QEMU,
            "-I", "../../../contrib/plugins",
            "-I", os.path.join(*[QEMU_INCLUDE_DIR, "qemu"]),
            "-I",QEMU_INCLUDE_DIR,
            *glib_flags,
            fname
        ])
        outval = "\n".join([i for i in out.decode().split("\n") if not i.startswith("# ")])
        outval = outval.replace("__attribute__((visibility(\"hidden\")))", "")
        outval = outval.replace("__attribute__((visibility(\"default\")))", "")
        outval = outval.replace("__attribute__((unused))", "")
        outval = outval.replace("__attribute__ ((constructor))", "")
        ffi.cdef(outval, override=True)

    define_messy_header(ffi,"pandare2/include/header.h")
    #define_messy_header(ffi,"../../../contrib/plugins/osi.h")

    # def define_clean_header(ffi, fname):
    #     '''Convenience function to pull in headers from file in C'''
    #     #print("Pulling cdefs from ", fname)
    #     # CFFI can't handle externs, but sometimes we have to extern C (as opposed to
    #     r = open(fname).read()
    #     for line in r.split("\n"):
    #         assert("extern \"C\" {" not in line), "Externs unsupported by CFFI. Change {} to a single line without braces".format(r)
    #     r = r.replace("extern \"C\" ", "") # This allows inline externs like 'extern "C" void foo(...)'

    #     reg = ppp_cb_typedef_regex()
    #     def expand_ppp_def(line):
    #         nonlocal reg
    #         m = reg.match(line)
    #         if m:
    #             ret_type = m.groups(1)[0]
    #             name = m.groups(1)[1]
    #             args = m.groups(1)[2]
    #             return typedef(ret_type, name, args)
    #         else:
    #             return line

    #     r = "\n".join([expand_ppp_def(line) for line in r.split("\n")])
    #     try:
    #         ffi.cdef(r)
    #     except Exception as e: # it's a cffi.CDefError, but cffi isn't imported
    #         print(f"\nError parsing header from {fname}\n")
    #         raise

    # # For OSI
    # ffi.cdef("typedef void GArray;")
    # ffi.cdef("typedef int target_pid_t;")

    # ffi.cdef("typedef uint"+str(bits)+"_t target_ulong;")
    # ffi.cdef("typedef int"+str(bits)+"_t target_long;")

    # # For direct access to -d logging flags
    # # unsigned is a lie. but it's the way QEMU treats it.
    # ffi.cdef("extern unsigned int qemu_loglevel;")
    # ffi.cdef("extern FILE* qemu_logfile;")

    # # PPP Headers
    # # Syscalls - load architecture-specific headers
    # if arch == "i386":
    #     define_clean_header(ffi, include_dir + "/panda_datatypes_X86_32.h")
    #     define_clean_header(ffi, include_dir + "/syscalls_ext_typedefs_x86.h")
    # elif arch == "x86_64":
    #     define_clean_header(ffi, include_dir + "/panda_datatypes_X86_64.h")
    #     define_clean_header(ffi, include_dir + "/syscalls_ext_typedefs_x64.h")
    # elif arch == "arm":
    #     define_clean_header(ffi, include_dir + "/panda_datatypes_ARM_32.h")
    #     define_clean_header(ffi, include_dir + "/syscalls_ext_typedefs_arm.h")

    # elif arch == "aarch64": # Could also do arch and bits==64
    #     define_clean_header(ffi, include_dir + "/panda_datatypes_ARM_64.h")
    #     define_clean_header(ffi, include_dir + "/syscalls_ext_typedefs_arm64.h")
    # elif arch == "ppc" and int(bits) == 32:
    #     define_clean_header(ffi, include_dir + "/panda_datatypes_PPC_32.h")
    #     print('WARNING: no syscalls support for PPC 32')
    # elif arch == "ppc" and int(bits) == 64:
    #     define_clean_header(ffi, include_dir + "/panda_datatypes_PPC_64.h")
    #     print('WARNING: no syscalls support for PPC 64')
    # elif arch == "mips" and int(bits) == 32:
    #     define_clean_header(ffi, include_dir + "/panda_datatypes_MIPS_32.h")
    #     define_clean_header(ffi, include_dir + "/syscalls_ext_typedefs_mips.h")
    # elif arch == "mipsel" and int(bits) == 32:
    #     define_clean_header(ffi, include_dir + "/panda_datatypes_MIPS_32.h") # XXX?
    #     define_clean_header(ffi, include_dir + "/syscalls_ext_typedefs_mips.h")
    # elif arch == "mips64" and int(bits) == 64:
    #     define_clean_header(ffi, include_dir + "/panda_datatypes_MIPS_64.h")
    #     define_clean_header(ffi, include_dir + "/syscalls_ext_typedefs_mips.h") # syscalls are the same?
    # else:
    #     print("PANDA_DATATYPES: Architecture not supported")

    # # Define some common panda datatypes
    # define_clean_header(ffi, include_dir + "/panda_datatypes.h")

    # # get some libc functionality
    # define_clean_header(ffi, include_dir + "/libc_includes.h")
    
    # # QEMU logging functionality
    # define_clean_header(ffi, include_dir + "/qlog.h")

    # # Now syscalls2 common:
    # define_clean_header(ffi, include_dir + "/syscalls2_info.h")

    # # A few more CFFI types now that we have common datatypes
    # # Manually define syscall_ctx_t - taken from syscalls2/generated/syscalls_ext_typedefs.h
    # # It uses a #DEFINES as part of the array size so CFFI can't hanle that :
    # ffi.cdef(''' typedef struct syscall_ctx { '''
    #         + f'''
    #         int no;               /**< number */
    #         target_ptr_t asid;    /**< calling process asid */
    #         target_ptr_t retaddr; /**< return address */
    #         uint8_t args[{GLOBAL_MAX_SYSCALL_ARG_SIZE}]
    #              [{GLOBAL_MAX_SYSCALL_ARG_SIZE}]; /**< arguments */
    #              '''
    #     +'''
    #     } syscall_ctx_t;
    # ''')
    # ffi.cdef("void panda_setup_signal_handling(void (*f) (int,void*,void*));",override=True)

    # # MANUAL curated list of PPP headers
    # define_clean_header(ffi, include_dir + "/syscalls_ext_typedefs.h")

    # define_clean_header(ffi, include_dir + "/callstack_instr.h")

    # define_clean_header(ffi, include_dir + "/hooks2_ppp.h")
    # define_clean_header(ffi, include_dir + "/proc_start_linux_ppp.h")
    # define_clean_header(ffi, include_dir + "/forcedexec_ppp.h")
    # define_clean_header(ffi, include_dir + "/stringsearch_ppp.h")
    # # END PPP headers

    # define_clean_header(ffi, include_dir + "/breakpoints.h")
    # for header in pypanda_headers:
    #     define_clean_header(ffi, header)
    
    # # has to be at the end because it depends on something in list
    # define_clean_header(ffi, include_dir + "/taint2.h")
    
    ffi.compile(verbose=True,debug=True,tmpdir='./pandare2/autogen')


def main(install=False,recompile=True):
    '''
    Copy and reformat panda header files into the autogen directory

    If `install` is set, we will assume files are being installed to the system
    Otherwise local paths are used.
    '''
    global pn
    pn = 1
    # examine all plugin dirs looking for pypanda-aware headers and pull
    # out pypanda bits to go in INCLUDE_DIR files
    # plugin_dirs = os.listdir(PLUGINS_DIR)

    INCLUDE_DIR_PYP_INSTALL = 'os.path.abspath(os.path.join(*[os.path.dirname(__file__), "..", "..", "pandare", "data", "pypanda", "include"]))'  # ... /python3.6/site-packages/panda/data/pypanda/include/

    # We want the various cpu_loop_... functions
#     create_pypanda_header("%s/exec/exec-all.h" % INCLUDE_DIR_CORE)

#     # Pull in osi/osi_types.h first - it's needed by other plugins too
#     if os.path.exists("%s/%s" % (PLUGINS_DIR, 'osi')):
#         print("Examining [%s] for pypanda-awareness" % 'osi_types.h')
#         create_pypanda_header("%s/osi/osi_types.h" % PLUGINS_DIR)
    
#     # Pull in taint2/addr.h first - it's needed by other plugins too
#     if os.path.exists("%s/%s" % (PLUGINS_DIR, 'taint2')):
#         print("Examining [%s] for pypanda-awareness" % 'addr.h')
#         create_pypanda_header("%s/taint2/addr.h" % PLUGINS_DIR)


#     for plugin in plugin_dirs:
#         if plugin == ".git": continue
#         plugin_dir = PLUGINS_DIR + "/" + plugin
#         if os.path.isdir(plugin_dir):
#             # just look for plugin_int_fns.h
#             plugin_file = plugin + "_int_fns.h"
#             if os.path.exists("%s/%s" % (plugin_dir, plugin_file)):
#                 print("Examining [%s] for pypanda-awareness" % plugin_file)
#                 create_pypanda_header("%s/%s" % (plugin_dir, plugin_file))

#     # Also pull in a few special header files outside of plugin-to-plugin APIs. Note we already handled syscalls2 above
#     for header in ["rr/rr_api.h", "plugin.h", "common.h", "rr/rr_types.h"]:
#         create_pypanda_header("%s/%s" % (INCLUDE_DIR_PAN, header))

#     # PPP headers
#     #   for syscalls2 ppp headers, grab generated files for all architectures
#     syscalls_gen_dir = PLUGINS_DIR + "/syscalls2/generated"
#     for header in os.listdir(syscalls_gen_dir):
#         if header.startswith("syscalls_ext_typedefs_"):
#             copy_ppp_header("%s/%s" % (syscalls_gen_dir, header))
#     create_pypanda_header("%s/%s" % (PLUGINS_DIR+"/syscalls2", "syscalls2_info.h"), no_record=True) # Get syscall_info_t, syscall_meta_t, syscall_argtype_t
#     copy_ppp_header("%s/%s" % (syscalls_gen_dir, "syscalls_ext_typedefs.h")) # Get a few arch-agnostic typedefs for PPP headers

#     #   other PPP headers: callstack_instr. TODO: manually currated list
#     copy_ppp_header("%s/%s" % (PLUGINS_DIR+"/callstack_instr", "callstack_instr.h"))

#     # XXX why do we have to append this to pypanda headers?
#     copy_ppp_header("%s/%s" % (PLUGINS_DIR+"/osi", "os_intro.h"))
#     pypanda_headers.append(os.path.join(INCLUDE_DIR_PYP, "os_intro.h"))

#     copy_ppp_header("%s/%s" % (PLUGINS_DIR+"/hooks2", "hooks2_ppp.h"))
#     # TODO: programtically copy anything that ends with _ppp.h
#     copy_ppp_header("%s/%s" % (PLUGINS_DIR+"/forcedexec",   "forcedexec_ppp.h"))
#     copy_ppp_header("%s/%s" % (PLUGINS_DIR+"/stringsearch", "stringsearch_ppp.h"))
#     create_pypanda_header("%s/%s" % (PLUGINS_DIR+"/hooks2", "hooks2.h"))
    
#     copy_ppp_header("%s/%s" % (PLUGINS_DIR+"/proc_start_linux", "proc_start_linux_ppp.h"))
#     create_pypanda_header("%s/%s" % (PLUGINS_DIR+"/proc_start_linux", "proc_start_linux.h"))
#     copy_ppp_header("%s/taint2/taint2.h" % PLUGINS_DIR)

#     with open(os.path.join(OUTPUT_DIR, "panda_datatypes.py"), "w") as pdty:
#         pdty.write("""
# \"\"\"
# Auto-generated type declaration to provide c-definitions for the cffi interface. It's highly unlikely you actually need this.
# If you simply need a list of callbacks consult the manual in main PANDA.
# \"\"\"
# # NOTE: panda_datatypes.py is auto generated by the script create_panda_datatypes.py
# # Please do not tinker with it!  Instead, fix the script that generates it

# # update DATATYPES_VERSION and corresponding check in pandare.panda._do_types_import
# # when this file changes and users will need to regenerate
# DATATYPES_VERSION = 1.1

# from enum import Enum
# from ctypes import *
# from collections import namedtuple

# class PandaState(Enum):
#     UNINT = 1
#     INIT_DONE = 2
#     IN_RECORD = 3
#     IN_REPLAY = 4
#     """)

#         cbn = 0
#         cb_list = {}
#         with open (os.path.join(INCLUDE_DIR_PAN, "callbacks/cb-defs.h")) as fp:
#             for line in fp:
#                 foo = re.search("^(\s+)PANDA_CB_([^,]+)\,", line)
#                 if foo:
#                     cbname = foo.groups()[1]
#                     cbname_l = cbname.lower()
#                     cb_list[cbn] = cbname_l
#                     cbn += 1

#         # Manually add callback 'last'
#         cb_list[cbn] = "last"
#         cbn += 1

#         pdty.write('\nPandaCB = namedtuple("PandaCB", "init \\\n')
#         for i in range(cbn-1):
#             pdty.write(cb_list[i] + " ")
#             if i == cbn-2:
#                 pdty.write('")\n')
#             else:
#                 pdty.write("\\\n")

#         # Analyze cb-def.sh to extract each entry in the 'typedef union panda_cb {...}'
#         # for each we grab the name, type
#         in_tdu = False
#         current_comment = ""
#         cb_types = {}
#         with open (os.path.join(INCLUDE_DIR_PAN, "callbacks/cb-defs.h")) as fp:
#             for line in fp:
#                 if re.search("typedef union panda_cb {", line):
#                     in_tdu = True
#                     continue
#                 if re.search("} panda_cb;", line):
#                     in_tdu = False
#                     continue

#                 if not in_tdu:
#                     continue

#                 # Accumulate comment strings - at the end of each, we'll
#                 # hit a function signature (match=True below) and empty buffer
#                 if re.search("\w*\/\* Callback ID:", line):
#                     cb_comment = line.split("/* Callback ID: ")[1].strip()
#                 elif len(cb_comment):
#                     cb_comment += "\n" + line.replace("*/","").strip()

#                 # Parse function signature:
#                 # example: int (*before_block_translate)(CPUState *env, target_ulong pc);
#                 # rv=int and params="CPUState* env, target_ulong pc"
#                 # partypes = [CPUState*, target_ulong]
#                 for i in range(cbn):
#                     match = re.search("^\s+(.*)\s+\(\*%s\)\((.*)\);" % cb_list[i], line)
#                     if match:
#                         rvtype = match.groups()[0]
#                         params = match.groups()[1]
#                         partypes = []
#                         for param in params.split(','):
#                             #This is basically

#                             typ = param
#                             for j in range(len(param)-1, -1, -1):
#                                 c = param[j]
#                                 if not (c.isalpha() or c.isnumeric() or c=='_'):
#                                     break
#                             else: # param is 'void' ?
#                                 typ = param.strip()

#                             partypes.append(typ)
#                         cb_typ = rvtype + " (" +  (", ".join(partypes)) + ")"
#                         cb_name = cb_list[i]

#                         assert(cb_comment.startswith("PANDA_CB_" + cb_name.upper())), f"Unaligned docs: callback {cb_name} has comment: {repr(cb_comment)}"
#                         cb_types[i] = (cb_name, cb_typ, cb_comment)
#                         cb_comment = ""

#         # Sanity check: each callback must exist in both panda_cb_type and function definition
#         for i in range(cbn-1):
#             if i not in cb_types:
#                 raise RuntimeError(f"Error parsing code for '{cb_list[i]}' in callbacks/cb-defs.h. Is it defined both in panda_cb_type enum and as a prototype later with the same name?")

#         # Define function to get function documentation
#         pdty.write("""
# def get_cb_docs():
#     ''' Generate a PCB of (return type, arg type, docstring) '''
#     return PandaCB(init = (None, None, None),\n""")

#         for i in range(cbn-1):
#             rv   = cb_types[i][1].split("(")[0].strip()
#             args = cb_types[i][1].split("(")[1].split(")")[0].strip()
#             pdty.write(f"    {cb_types[i][0]} = ({repr(rv)}, {repr(args)}, {repr(cb_types[i][2])})")
#             if i == cbn-2:
#                 pdty.write(")\n")
#             else:
#                 pdty.write(",\n")

#         # Define function to get pcb object populated with callback names + ffi.callback("signature")
#         pdty.write("""
# def get_cbs(ffi):
#     '''
#     Returns (callback_dictory, pcb) tuple of ({callback name: CFFI func}, CFFI funcs)
#     '''

#     # So we need access to some data structures, but don't actually
#     # want to open all of libpanda yet because we don't have all the
#     # file information. So we just open libc to access this.
#     C = ffi.dlopen(None)

#     # Stores the names and numbers for callbacks
#     pandacbtype = namedtuple("pandacbtype", "name number")

#     pcb = PandaCB(init = ffi.callback("bool(void*)"),
# """)

#         for i in range(cbn-1):
#             pdty.write('    %s = ffi.callback("%s")' % cb_types[i][:2])
#             if i == cbn-2:
#                 pdty.write(")\n")
#             else:
#                 pdty.write(",\n")

#         pdty.write("""

#     callback_dictionary = {
#         pcb.init : pandacbtype("init", -1),
# """)


#         for i in range(cbn-1):
#             cb_name = cb_list[i]
#             cb_name_up = cb_name.upper()
#             pdty.write('        pcb.%s : pandacbtype("%s", C.PANDA_CB_%s)' % (cb_name, cb_name, cb_name_up))
#             if i == cbn-2:
#                 pdty.write("}\n")
#             else:
#                 pdty.write(",\n")

#         pdty.write("    return (pcb, callback_dictionary)")


#     #########################################################
#     #########################################################
#     # second, create panda_datatypes.h by glomming together
#     # files in panda/include/panda

#     with open(os.path.join(INCLUDE_DIR_PYP, "panda_datatypes.h"), "w") as pdth:

#         pdth.write("""
# // NOTE: panda_datatypes.h is auto generated by the script create_panda_datatypes.py
# // Please do not tinker with it!  Instead, fix the script that generates it

# #define PYPANDA 1

# """)
#         # probably a better way...
#         pdth.write("typedef target_ulong target_ptr_t;\n")

#         # XXX: These are defined in plugin.h, but we can't include all of plugin.h
#         #      here without redefining things. Necessary for something? cb-defs?
#         pdth.write("#define MAX_PANDA_PLUGINS 16\n")
#         pdth.write("#define MAX_PANDA_PLUGIN_ARGS 32\n")


#         for filename in ["callbacks/cb-defs.h",
#                         f"{PLUGINS_DIR}/osi_linux/utils/kernelinfo/kernelinfo.h",
#                          "panda_api.h", ]:
#             include_this(pdth, filename)
    
    if recompile:
        print("Recompiling headers for cffi")
        arches = [
            ("i386", 32),
            ("x86_64", 64),
            ("arm", 32),
            ("aarch64", 64),
            ("ppc", 32),
            ("ppc", 64),
            ("mips", 32),
            ("mipsel",32),
            ("mips64",64),
        ]

        from multiprocessing import Process
        ps = (
            Process(target=compile, args=(arch[0], arch[1], pypanda_headers, install, INCLUDE_DIR_PYP))
            for arch in arches
        )
        [p.start() for p in ps]
        [p.join() for p in ps]


if __name__ == '__main__':
    main()
