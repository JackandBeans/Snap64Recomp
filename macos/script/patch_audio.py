"""Route the original game's AI length read to the host audio backlog word."""
from pathlib import Path
p = Path(__file__).resolve().parents[2] / 'RecompiledFuncs/funcs_48.c'
s = p.read_text()
old = 'ctx->r24 = S32(0XA450 << 16);'
new = 'ctx->r24 = S32(0X80C0 << 16);'
if old in s:
    assert s.count(old) == 1
    start = s.index(old)
    prefix, tail = s[:start], s[start:]
    assert 'ctx->r14 = MEM_W(ctx->r24, 0X4);' in tail[:600]
    tail = tail.replace(old, new, 1).replace('ctx->r14 = MEM_W(ctx->r24, 0X4);', 'ctx->r14 = MEM_W(ctx->r24, 0X40);', 1)
    p.write_text(prefix + tail)
else:
    assert new in s, 'Generated audio code changed; inspect before applying the patch.'
