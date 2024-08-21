from collections import namedtuple
import gdb
from gdb.FrameDecorator import FrameDecorator
from gdb.unwinder import Unwinder, FrameId

def initPoser():
    PoserFilter()
    gdb.unwinder.register_unwinder(None, PoserUnwinder(), True)

TRAP_VECTORS = 32
SYSCALL_TRAP = 15
SYSCALL_ADDR = (TRAP_VECTORS + SYSCALL_TRAP) * 4

FrameFunction = namedtuple("FrameFunction", ["start", "end", "name"])

def get_remote():
    conn = gdb.selected_inferior().connection
    return conn if isinstance(conn, gdb.RemoteTargetConnection) else None

def get_frame_func_from_pc(pc):
    conn = get_remote()
    response = conn.send_packet("qposer.Frame:%x" % pc) if conn else None
    if response is None:
        return None

    response = response.decode("ascii")
    frame_func = FrameFunction(int(response[0:8], 16), int(response[8:16], 16), response[16:])
    return frame_func if frame_func.start or frame_func.end or frame_func.name else None

class PoserUnwinder(Unwinder):
    def __init__(self):
        super().__init__("PoserUnwinder")

    def __call__(self, pending_frame):
        pc = pending_frame.read_register("pc")
        sp = pending_frame.read_register("sp")

        # If block_for_pc works, then that means that GDB has an image with
        # debug information, and its own unwinding should be used, since it
        # will probably do a better job (and will be faster since it will not
        # need to query the remote server here for the frame function)
        if gdb.current_progspace().block_for_pc(pc) is not None:
            return None

        u32_ty = pending_frame.architecture().integer_type(32, False)
        u32ptr_ty = u32_ty.pointer()

        # RIP hopes and dreams:
        # https://sourceware.org/bugzilla/show_bug.cgi?id=32120
        # frame_func = get_frame_func_from_pc(pc)

        base = pending_frame.read_register("fp").cast(u32ptr_ty)

        syscall_trap = gdb.Value(SYSCALL_ADDR).cast(u32ptr_ty)[0]
        if pc == syscall_trap:
            next_pc = (sp + 2).cast(u32ptr_ty)[0] + 2
        else:
            next_pc = base[1]

        unwind_info = pending_frame.create_unwind_info(FrameId(sp, pc))
        unwind_info.add_saved_register("fp", base[0])
        unwind_info.add_saved_register("pc", next_pc)
        unwind_info.add_saved_register("sp", base[2])
        return unwind_info

class PoserFilter():
    def __init__(self):
        self.name = "PoserFilter"
        self.priority = 100
        self.enabled = True
        gdb.frame_filters[self.name] = self

    def filter(self, frame_iter):
        frame_iter = map(PoserFrameDecorator, frame_iter)
        return frame_iter

class PoserFrameDecorator(FrameDecorator):
    def function(self):
        frame = self.inferior_frame()
        name = frame.name()

        if name is not None:
            return name

        func = get_frame_func_from_pc(frame.pc())
        return func.name if func is not None else "??"

initPoser()
