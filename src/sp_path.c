#include "sp_internal.h"

static int sp__is_drive_letter(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

int sp_path_is_sep(char c, sp_path_style style) {
    if (c == '/') return 1;
    if (style == SP_WINDOWS && c == '\\') return 1;
    return 0;
}

char sp_path_pref_sep(sp_path_style style) {
    return style == SP_WINDOWS ? '\\' : '/';
}

static size_t sp__skip_seps(sp_view p, size_t i, sp_path_style st) {
    while (i < p.len && sp_path_is_sep(p.ptr[i], st)) i++;
    return i;
}

static size_t sp__skip_comp(sp_view p, size_t i, sp_path_style st) {
    while (i < p.len && !sp_path_is_sep(p.ptr[i], st)) i++;
    return i;
}

static int sp__win_unc_prefix(sp_view p) {
    return p.len >= 2 && sp_path_is_sep(p.ptr[0], SP_WINDOWS) &&
           sp_path_is_sep(p.ptr[1], SP_WINDOWS);
}

/* Consume server + share starting at `i` (first char of server). Returns end index. */
static size_t sp__win_share_end(sp_view p, size_t i) {
    size_t s = sp__skip_comp(p, i, SP_WINDOWS);
    size_t k, e;
    if (s == i) return i;
    if (s >= p.len) return s;
    k = sp__skip_seps(p, s, SP_WINDOWS);
    if (k >= p.len) return s;
    e = sp__skip_comp(p, k, SP_WINDOWS);
    if (e == k) return s;
    return e;
}

static int sp__starts_unc_word(sp_view p, size_t i) {
    if (i + 4u > p.len) return 0;
    return (p.ptr[i] == 'U' || p.ptr[i] == 'u') &&
           (p.ptr[i + 1u] == 'N' || p.ptr[i + 1u] == 'n') &&
           (p.ptr[i + 2u] == 'C' || p.ptr[i + 2u] == 'c') &&
           sp_path_is_sep(p.ptr[i + 3u], SP_WINDOWS);
}

static size_t sp__win_volume_len(sp_view p) {
    if (p.len >= 2u && p.ptr[1] == ':' && sp__is_drive_letter(p.ptr[0])) return 2u;

    if (!sp__win_unc_prefix(p)) return 0;

    /* \\?\ ...  or  //?/ ... */
    if (p.len >= 4u && p.ptr[2] == '?' && sp_path_is_sep(p.ptr[3], SP_WINDOWS)) {
        if (sp__starts_unc_word(p, 4u)) {
            size_t e = sp__win_share_end(p, 8u);
            return e > 8u ? e : 8u;
        }
        return sp__skip_comp(p, 4u, SP_WINDOWS);
    }

    /* \\.\device */
    if (p.len >= 4u && p.ptr[2] == '.' && sp_path_is_sep(p.ptr[3], SP_WINDOWS)) {
        return sp__skip_comp(p, 4u, SP_WINDOWS);
    }

    return sp__win_share_end(p, 2u);
}

static size_t sp__volume_len(sp_view p, sp_path_style st) {
    if (st == SP_WINDOWS) return sp__win_volume_len(p);
    return 0;
}

sp_path_info sp_path_parse(sp_view path, sp_path_style style) {
    sp_path_info inf;
    size_t vlen;
    memset(&inf, 0, sizeof(inf));
    if (!path.ptr) path = sp_view_make("", 0);

    vlen = sp__volume_len(path, style);
    inf.volume = sp_view_make(path.ptr, vlen);
    inf.rest = sp_view_make(path.ptr + vlen, path.len - vlen);
    inf.rooted = inf.rest.len > 0 && sp_path_is_sep(inf.rest.ptr[0], style);
    inf.unc = 0;
    inf.drive_rel = 0;
    if (style == SP_WINDOWS && vlen >= 2u) {
        inf.unc = sp__win_unc_prefix(inf.volume);
        inf.drive_rel = (vlen == 2u && inf.volume.ptr[1] == ':' && !inf.rooted && !inf.unc);
    }
    return inf;
}

int sp_path_is_rooted(sp_view path, sp_path_style style) {
    return sp_path_parse(path, style).rooted;
}

int sp_path_has_volume(sp_view path, sp_path_style style) {
    return sp_path_parse(path, style).volume.len > 0;
}

int sp_path_is_abs(sp_view path, sp_path_style style) {
    sp_path_info inf = sp_path_parse(path, style);
    return inf.rooted || inf.unc;
}

static int sp__path_byte_eq(char a, char b, sp_path_style st) {
    if (sp_path_is_sep(a, st) && sp_path_is_sep(b, st)) return 1;
    if (st == SP_WINDOWS) return sp_ascii_eq_ci(a, b);
    return a == b;
}

int sp_path_eq(sp_view a, sp_view b, sp_path_style style) {
    size_t i;
    if (a.len != b.len) return 0;
    if (!a.ptr) a.ptr = "";
    if (!b.ptr) b.ptr = "";
    for (i = 0; i < a.len; i++) {
        if (!sp__path_byte_eq(a.ptr[i], b.ptr[i], style)) return 0;
    }
    return 1;
}

static int sp__path_prefix_eq(sp_view child, sp_view parent, sp_path_style st) {
    size_t i;
    if (parent.len > child.len) return 0;
    for (i = 0; i < parent.len; i++) {
        if (!sp__path_byte_eq(child.ptr[i], parent.ptr[i], st)) return 0;
    }
    return 1;
}

static void sp__append_volume_normalized(sp_buf *b, sp_view vol, sp_path_style st) {
    size_t i;
    char sep = sp_path_pref_sep(st);
    for (i = 0; i < vol.len; i++) {
        char c = vol.ptr[i];
        if (sp_path_is_sep(c, st)) sp_buf_append_char(b, sep);
        else sp_buf_append_char(b, c);
    }
}

static int sp__comp_dot(sp_view c) {
    return c.len == 1 && c.ptr[0] == '.';
}

static int sp__comp_dotdot(sp_view c) {
    return c.len == 2 && c.ptr[0] == '.' && c.ptr[1] == '.';
}

sp_str sp_path_clean(sp_arena *a, sp_view path, sp_path_style style) {
    sp_path_info inf;
    sp_view *stack;
    size_t top = 0, i, bytes;
    int rooted;
    sp_buf b;
    char sep;

    if (!a) return sp_str_error(SP_ERR_INVALID);
    if (!path.ptr) path = sp_view_make("", 0);

    inf = sp_path_parse(path, style);
    rooted = inf.rooted || inf.unc;

    {
        size_t nstack;
        if (!sp_add_ok(path.len, 2u, &nstack) || !sp_mul_ok(nstack, sizeof(sp_view), &bytes))
            return sp_str_error(SP_ERR_OVERFLOW);
    }
    stack = (sp_view *)sp_arena_alloc(a, bytes, sizeof(void *));
    if (!stack) return sp_str_error(SP_ERR_OOM);

    i = 0;
    while (i < inf.rest.len) {
        sp_view comp;
        size_t e;
        if (sp_path_is_sep(inf.rest.ptr[i], style)) {
            i = sp__skip_seps(inf.rest, i, style);
            continue;
        }
        e = sp__skip_comp(inf.rest, i, style);
        comp = sp_view_make(inf.rest.ptr + i, e - i);
        i = e;
        if (sp__comp_dot(comp)) continue;
        if (sp__comp_dotdot(comp)) {
            if (top > 0 && !sp__comp_dotdot(stack[top - 1u])) {
                top--;
            } else if (!rooted) {
                stack[top++] = comp;
            }
            continue;
        }
        stack[top++] = comp;
    }

    sep = sp_path_pref_sep(style);
    sp_buf_init(&b, a);
    sp__append_volume_normalized(&b, inf.volume, style);
    if (inf.rooted && !(inf.unc && top == 0)) sp_buf_append_char(&b, sep);
    else if (inf.unc && top > 0 && (b.len == 0 || !sp_path_is_sep(b.ptr[b.len - 1u], style)))
        sp_buf_append_char(&b, sep);
    for (i = 0; i < top; i++) {
        if (i > 0) sp_buf_append_char(&b, sep);
        sp_buf_append(&b, stack[i]);
    }

    if (b.len == 0) {
        sp_buf_append_char(&b, '.');
    } else if (style == SP_WINDOWS && inf.volume.len == 2u && inf.volume.ptr[1] == ':' &&
               !inf.rooted && top == 0) {
        sp_buf_append_char(&b, '.');
    }

    return sp_buf_finish(&b);
}

static int sp__win_invalid_char(char c) {
    return c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*';
}

sp_err sp_path_check(sp_view path, sp_path_style style, unsigned sec_flags) {
    size_t i;
    SP_UNUSED(style);
    if (path.len && !path.ptr) return SP_ERR_INVALID;
    if (sp_view_contains_byte(path, 0)) return SP_ERR_NUL;
    if ((sec_flags & SP_SEC_UTF8) && path.len && !sp_utf8_valid(path)) return SP_ERR_UTF8;
    if (sec_flags & SP_SEC_CTRL) {
        for (i = 0; i < path.len; i++) {
            unsigned char c = (unsigned char)path.ptr[i];
            if (c < 0x20u || c == 0x7Fu) return SP_ERR_INVALID;
        }
    }
    if (style == SP_WINDOWS) {
        sp_path_info inf = sp_path_parse(path, style);
        for (i = 0; i < inf.rest.len; i++) {
            char c = inf.rest.ptr[i];
            if (c == ':' || sp__win_invalid_char(c)) return SP_ERR_INVALID;
        }
    }
    return SP_OK;
}

static int sp__is_clean_root(sp_view cleaned, sp_path_style style) {
    sp_path_info inf = sp_path_parse(cleaned, style);
    if (!(inf.rooted || inf.unc)) return 0;
    if (inf.rest.len == 0) return 1;
    if (inf.rest.len == 1 && sp_path_is_sep(inf.rest.ptr[0], style)) return 1;
    return 0;
}

static int sp__is_under_cleaned(sp_view p, sp_view c, sp_path_style style) {
    sp_path_info pi, ci;
    if (sp_path_eq(p, c, style)) return 1;
    pi = sp_path_parse(p, style);
    ci = sp_path_parse(c, style);
    if (!sp_path_eq(pi.volume, ci.volume, style)) return 0;
    if (sp__is_clean_root(p, style)) return ci.rooted || ci.unc;
    if (sp__path_prefix_eq(c, p, style) && c.len > p.len && sp_path_is_sep(c.ptr[p.len], style))
        return 1;
    if (sp_view_eq_cstr(p, ".")) {
        if (!ci.rooted && !ci.unc && !sp_view_eq_cstr(c, "..") &&
            !sp_view_starts_with(c, style == SP_WINDOWS ? SP_LIT("..\\") : SP_LIT("../")))
            return 1;
    }
    return 0;
}

int sp_path_is_under(sp_arena *a, sp_view parent, sp_view child, sp_path_style style) {
    sp_mark m;
    sp_str p, c;
    int r;
    if (!a) return 0;
    m = sp_arena_mark(a);
    p = sp_path_clean(a, parent, style);
    c = sp_path_clean(a, child, style);
    r = (p.err == SP_OK && c.err == SP_OK) ? sp__is_under_cleaned(sp_str_view(p), sp_str_view(c), style)
                                           : 0;
    sp_arena_rewind(a, m);
    return r;
}

static int sp__user_escapes(sp_view cleaned_user, sp_path_style style) {
    sp_path_info inf = sp_path_parse(cleaned_user, style);
    if (inf.rooted || inf.unc || inf.volume.len) return 1;
    if (sp_view_eq_cstr(cleaned_user, "..")) return 1;
    if (style == SP_WINDOWS)
        return sp_view_starts_with(cleaned_user, SP_LIT("..\\"));
    return sp_view_starts_with(cleaned_user, SP_LIT("../"));
}

static int sp__win_bad_components(sp_view cleaned) {
    sp_path_info inf = sp_path_parse(cleaned, SP_WINDOWS);
    size_t i = 0;
    while (i < inf.rest.len) {
        size_t e;
        sp_view comp;
        if (sp_path_is_sep(inf.rest.ptr[i], SP_WINDOWS)) {
            i++;
            continue;
        }
        e = i;
        while (e < inf.rest.len && !sp_path_is_sep(inf.rest.ptr[e], SP_WINDOWS)) e++;
        comp = sp_view_make(inf.rest.ptr + i, e - i);
        if (comp.len && (comp.ptr[comp.len - 1u] == '.' || comp.ptr[comp.len - 1u] == ' ') &&
            !sp__comp_dot(comp) && !sp__comp_dotdot(comp))
            return 1;
        i = e;
    }
    return 0;
}

sp_str sp_path_join_under(sp_arena *a, sp_view root, sp_view user,
                          sp_path_style style, unsigned sec_flags) {
    sp_err e;
    sp_str cr, cu, joined;
    sp_view parts[2];

    if (!a) return sp_str_error(SP_ERR_INVALID);
    e = sp_path_check(user, style, sec_flags);
    if (e != SP_OK) return sp_str_error(e);
    e = sp_path_check(root, style, sec_flags);
    if (e != SP_OK) return sp_str_error(e);

    {
        sp_path_info ui = sp_path_parse(user, style);
        if (ui.rooted || ui.unc || ui.volume.len) return sp_str_error(SP_ERR_ABSOLUTE);
    }

    cr = sp_path_clean(a, root, style);
    if (cr.err != SP_OK) return cr;
    if (cr.len == 1 && cr.ptr[0] == '.') return sp_str_error(SP_ERR_INVALID);

    cu = sp_path_clean(a, user, style);
    if (cu.err != SP_OK) return cu;
    if (sp__user_escapes(sp_str_view(cu), style)) return sp_str_error(SP_ERR_TRAVERSAL);
    if (style == SP_WINDOWS && sp__win_bad_components(sp_str_view(cu)))
        return sp_str_error(SP_ERR_INVALID);

    parts[0] = sp_str_view(cr);
    parts[1] = sp_str_view(cu);
    joined = sp_path_join(a, style, parts, 2);
    if (joined.err != SP_OK) return joined;
    if (!sp__is_under_cleaned(sp_str_view(cr), sp_str_view(joined), style))
        return sp_str_error(SP_ERR_TRAVERSAL);
    return joined;
}

sp_str sp_path_join(sp_arena *a, sp_path_style style, const sp_view *parts, size_t n) {
    sp_buf b;
    size_t i;
    sp_view drive;
    int have = 0;

    if (!a) return sp_str_error(SP_ERR_INVALID);
    if (n && !parts) return sp_str_error(SP_ERR_INVALID);

    sp_buf_init(&b, a);
    drive = sp_view_make("", 0);

    for (i = 0; i < n; i++) {
        sp_view p = parts[i];
        sp_path_info inf;
        if (!p.ptr) p = sp_view_make("", 0);
        if (p.len == 0) continue;
        inf = sp_path_parse(p, style);

        if (style == SP_WINDOWS) {
            if (inf.volume.len) {
                drive = inf.volume;
                /* replace path with this part's rest, keep new drive */
                sp_buf_init(&b, a);
                sp__append_volume_normalized(&b, drive, style);
                sp_buf_append(&b, inf.rest);
                have = 1;
                continue;
            }
            if (inf.rooted) {
                /* discard previous path but keep drive */
                sp_buf_init(&b, a);
                sp__append_volume_normalized(&b, drive, style);
                sp_buf_append(&b, p);
                have = 1;
                continue;
            }
        } else {
            if (inf.rooted) {
                sp_buf_init(&b, a);
                sp_buf_append(&b, p);
                have = 1;
                continue;
            }
        }

        if (!have) {
            sp_buf_append(&b, p);
            have = 1;
        } else {
            if (b.len == 0 || !sp_path_is_sep(b.ptr[b.len - 1u], style)) {
                sp_buf_append_char(&b, sp_path_pref_sep(style));
            }
            sp_buf_append(&b, p);
        }
    }

    if (!have) return sp_path_clean(a, SP_LIT(""), style);
    return sp_path_clean(a, sp_str_view(sp_buf_finish(&b)), style);
}

sp_str sp_path_join2(sp_arena *a, sp_path_style style, sp_view x, sp_view y) {
    sp_view v[2];
    v[0] = x;
    v[1] = y;
    return sp_path_join(a, style, v, 2);
}

sp_str sp_path_join3(sp_arena *a, sp_path_style style, sp_view x, sp_view y, sp_view z) {
    sp_view v[3];
    v[0] = x;
    v[1] = y;
    v[2] = z;
    return sp_path_join(a, style, v, 3);
}

sp_str sp_path_cat(sp_arena *a, sp_path_style style, const sp_view *parts, size_t n) {
    sp_buf b;
    size_t i;
    char sep;
    if (!a) return sp_str_error(SP_ERR_INVALID);
    if (n && !parts) return sp_str_error(SP_ERR_INVALID);
    sp_buf_init(&b, a);
    sep = sp_path_pref_sep(style);
    for (i = 0; i < n; i++) {
        sp_view p = parts[i];
        if (!p.ptr) p = sp_view_make("", 0);
        if (p.len == 0) continue;
        if (b.len > 0 && !sp_path_is_sep(b.ptr[b.len - 1u], style) &&
            !sp_path_is_sep(p.ptr[0], style)) {
            sp_buf_append_char(&b, sep);
        }
        sp_buf_append(&b, p);
    }
    if (b.len == 0) return sp_path_clean(a, SP_LIT(""), style);
    return sp_path_clean(a, sp_str_view(sp_buf_finish(&b)), style);
}

sp_view sp_path_base(sp_view path, sp_path_style style) {
    sp_path_info inf;
    sp_view p;
    size_t i;
    if (!path.ptr) path = sp_view_make("", 0);
    if (path.len == 0) return SP_LIT(".");
    inf = sp_path_parse(path, style);
    p = path;
    while (p.len > inf.volume.len && sp_path_is_sep(p.ptr[p.len - 1u], style)) p.len--;
    if (p.len == inf.volume.len) {
        if (inf.rooted || inf.unc) {
            static const char sl[2] = {'/', '\\'};
            return sp_view_make(style == SP_WINDOWS ? &sl[1] : &sl[0], 1);
        }
        return SP_LIT(".");
    }
    i = p.len;
    while (i > inf.volume.len && !sp_path_is_sep(p.ptr[i - 1u], style)) i--;
    return sp_view_make(p.ptr + i, p.len - i);
}

sp_view sp_path_dir(sp_view path, sp_path_style style) {
    sp_path_info inf;
    size_t min_len;
    if (!path.ptr) path = sp_view_make("", 0);
    if (path.len == 0) return SP_LIT(".");
    inf = sp_path_parse(path, style);
    min_len = inf.volume.len;
    if (inf.rooted) min_len += 1u;

    while (path.len > min_len && sp_path_is_sep(path.ptr[path.len - 1u], style)) path.len--;
    while (path.len > min_len && !sp_path_is_sep(path.ptr[path.len - 1u], style)) path.len--;
    while (path.len > min_len && sp_path_is_sep(path.ptr[path.len - 1u], style)) path.len--;

    if (inf.unc && path.len <= inf.volume.len + 1u) return inf.volume;
    if (path.len == inf.volume.len) {
        if (inf.volume.len) return inf.volume;
        return SP_LIT(".");
    }
    if (path.len == 0) return SP_LIT(".");
    return path;
}

sp_view sp_path_ext(sp_view path, sp_path_style style) {
    sp_view base = sp_path_base(path, style);
    size_t i;
    if (base.len == 0 || (base.len == 1 && base.ptr[0] == '.')) return sp_view_make("", 0);
    i = base.len;
    while (i > 0) {
        char c = base.ptr[i - 1u];
        if (c == '.') {
            if (i == 1) return sp_view_make("", 0); /* .bashrc */
            return sp_view_make(base.ptr + i - 1u, base.len - (i - 1u));
        }
        if (sp_path_is_sep(c, style)) break;
        i--;
    }
    return sp_view_make("", 0);
}

sp_view sp_path_stem(sp_view path, sp_path_style style) {
    sp_view base = sp_path_base(path, style);
    sp_view ext = sp_path_ext(path, style);
    if (ext.len >= base.len) return base;
    return sp_view_make(base.ptr, base.len - ext.len);
}

static sp_view *sp__split_comps(sp_arena *a, sp_view rest, sp_path_style st, size_t *n) {
    size_t i, k = 0, cap, bytes;
    sp_view *out;
    cap = rest.len / 1u + 1u;
    if (!sp_mul_ok(cap, sizeof(sp_view), &bytes)) return NULL;
    out = (sp_view *)sp_arena_alloc(a, bytes, sizeof(void *));
    if (!out) return NULL;
    i = sp__skip_seps(rest, 0, st);
    while (i < rest.len) {
        size_t e = sp__skip_comp(rest, i, st);
        out[k++] = sp_view_make(rest.ptr + i, e - i);
        i = sp__skip_seps(rest, e, st);
    }
    *n = k;
    return out;
}

sp_str sp_path_rel(sp_arena *a, sp_view from, sp_view to, sp_path_style style) {
    sp_str cf, ct;
    sp_path_info fi, ti;
    sp_view *fcomp, *tcomp;
    size_t nf = 0, nt = 0, i, common;
    sp_buf b;
    char sep;

    if (!a) return sp_str_error(SP_ERR_INVALID);
    cf = sp_path_clean(a, from, style);
    ct = sp_path_clean(a, to, style);
    if (cf.err != SP_OK) return cf;
    if (ct.err != SP_OK) return ct;

    fi = sp_path_parse(sp_str_view(cf), style);
    ti = sp_path_parse(sp_str_view(ct), style);
    if ((fi.rooted || fi.unc) != (ti.rooted || ti.unc)) return sp_str_error(SP_ERR_INVALID);
    if (!sp_path_eq(fi.volume, ti.volume, style)) return sp_str_error(SP_ERR_INVALID);

    fcomp = sp__split_comps(a, fi.rest, style, &nf);
    tcomp = sp__split_comps(a, ti.rest, style, &nt);
    if (!fcomp || !tcomp) return sp_str_error(SP_ERR_OOM);

    common = 0;
    while (common < nf && common < nt && sp_path_eq(fcomp[common], tcomp[common], style)) {
        common++;
    }

    if (common == nf && common == nt) return sp_str_from(a, ".");

    sp_buf_init(&b, a);
    sep = sp_path_pref_sep(style);
    for (i = common; i < nf; i++) {
        if (sp__comp_dotdot(fcomp[i])) return sp_str_error(SP_ERR_INVALID);
        if (b.len) sp_buf_append_char(&b, sep);
        sp_buf_append(&b, SP_LIT(".."));
    }
    for (i = common; i < nt; i++) {
        if (b.len) sp_buf_append_char(&b, sep);
        sp_buf_append(&b, tcomp[i]);
    }
    if (b.len == 0) return sp_str_from(a, ".");
    return sp_buf_finish(&b);
}
