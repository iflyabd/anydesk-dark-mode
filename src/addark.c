/* AnyDesk dark-mode shim.
 *
 * AnyDesk 8.1 on Linux has no dark mode: it builds GTK CSS at runtime with
 * hardcoded light colours (rgba(240,240,240,...), white surfaces, black text),
 * and its custom widgets paint flat surfaces with cairo using fixed light
 * colours (#F0F0F0 fill, white cards) that ignore the system GTK theme.
 *
 * This shim is preloaded only for the AnyDesk GUI (see bin/anydesk-dark):
 *   1. gtk_css_provider_load_from_data()  -> rewrites the generated CSS.
 *   2. cairo_set_source_rgb[a]()          -> darkens opaque light fills, but
 *      never while Pango is drawing text (so labels stay readable).
 *   3. gdk_cairo_set_source_pixbuf()      -> inverts small monochrome glyph
 *      pixbufs (toolbar icons) so they stay visible on dark buttons.
 *
 * Other GTK applications are unaffected. Remove the preload for the stock look.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dlfcn.h>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

/* ------------------------------------------------------------------ */
/* 1. CSS rewriting                                                    */
/* ------------------------------------------------------------------ */

typedef int (*css_load_fn)(void *, const char *, long, void **);

struct repl { const char *from; const char *to; };

/* Order matters: longer/more specific patterns first. */
static const struct repl REPLACEMENTS[] = {
    /* ---- surfaces (only fully opaque light greys) ---- */
    { "rgba (240,240,240,1.00)", "rgba (26,27,30,1.00)"  },  /* window bg      */
    { "rgba (232,232,232,1.00)", "rgba (38,40,45,1.00)"  },  /* tab strip      */
    { "rgba (250,250,250,1.00)", "rgba (35,36,40,1.00)"  },
    { "rgba (255,255,255,1.00)", "rgba (33,34,38,1.00)"  },  /* cards/entries  */
    { "rgba (203,204,201,1.00)", "rgba (52,55,61,1.00)"  },  /* grey panels    */
    { "rgba (204,204,204,1.00)", "rgba (55,58,63,1.00)"  },
    { "rgba (239,243,250,1.00)", "rgba (38,44,56,1.00)"  },
    { "rgba (150,150,150,1.00)", "rgba (92,97,104,1.00)" },

    /* ---- text ---- */
    { "color: black",   "color: #E6E8EB" },
    { "color: #000000", "color: #E6E8EB" },
    { "alpha(black",    "alpha(#E6E8EB"  },
    { "color: #818181", "color: #A9AEB6" },
    { "color: #6E6E6E", "color: #A9AEB6" },
    { "color: #7d7d7d", "color: #A9AEB6" },
    { "color: #999999", "color: #B6BAC1" },

    /* ---- named palette used by the app's own widgets ---- */
    { "@define-color ad_color_white #FFFFFF",                "@define-color ad_color_white #212226" },
    { "@define-color ad_color_lightgrey #E8E8E8",            "@define-color ad_color_lightgrey #26282D" },
    { "@define-color ad_color_grey #CCD0C4",                 "@define-color ad_color_grey #34373D" },
    { "@define-color ad_default_background_color #F0F0F0",   "@define-color ad_default_background_color #1A1B1E" },
    { "@define-color ad_default_button_color @ad_color_lightgrey", "@define-color ad_default_button_color #2E3138" },
    { "@define-color ad_disabled_button_color @ad_color_lightgrey", "@define-color ad_disabled_button_color #24262B" },

    /* ---- flat widget templates generated at runtime ---- */
    { "background: #FFFFFF",       "background: #212226"        },
    { "background-color: #FFFFFF", "background-color: #212226"  },
    { "border: 0px solid #FFFFFF", "border: 0px solid #212226"  },
    { "border: 2px solid#E6E6E6",  "border: 2px solid #2A2C31"  },
    { "border: 2px solid #E6E6E6", "border: 2px solid #2A2C31"  },
    { "#E6E6E6",                   "#2A2C31"                    },
    { "rgba(255, 255, 255, 0.6)",  "rgba(58, 61, 66, 0.90)"     },
    { "caret-color: black",        "caret-color: #E6E8EB"       },
    { "rgb(255, 255, 255)",        "rgb(33, 34, 38)"            },
    { "rgb(255,255,255)",          "rgb(33,34,38)"              },
};

static char *rewrite(const char *src) {
    size_t cap = strlen(src) + 1;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    strcpy(buf, src);

    for (size_t i = 0; i < sizeof(REPLACEMENTS) / sizeof(REPLACEMENTS[0]); i++) {
        const char *from = REPLACEMENTS[i].from;
        const char *to   = REPLACEMENTS[i].to;
        size_t fl = strlen(from), tl = strlen(to);
        char *p = buf;
        while ((p = strstr(p, from)) != NULL) {
            size_t tail = strlen(p + fl);
            size_t need = (size_t)(p - buf) + tl + tail + 1;
            if (need > cap) {
                cap = need * 2;
                char *nb = realloc(buf, cap);
                if (!nb) { free(buf); return NULL; }
                p = nb + (p - buf);
                buf = nb;
            }
            memmove(p + tl, p + fl, tail + 1);
            memcpy(p, to, tl);
            p += tl;
        }
    }
    return buf;
}

int gtk_css_provider_load_from_data(void *prov, const char *data, long len, void **err) {
    static css_load_fn real;
    if (!real)
        real = (css_load_fn)dlsym(RTLD_NEXT, "gtk_css_provider_load_from_data");

    if (!data || !*data)
        return real(prov, data, len, err);

    char *src;
    if (len > 0) {
        src = malloc((size_t)len + 1);
        if (!src) return real(prov, data, len, err);
        memcpy(src, data, (size_t)len);
        src[len] = '\0';
    } else {
        src = strdup(data);
        if (!src) return real(prov, data, len, err);
    }

    char *out = rewrite(src);
    int rc;
    if (out) {
        rc = real(prov, out, -1, err);
        free(out);
    } else {
        rc = real(prov, src, len, err);
    }
    free(src);
    return rc;
}

/* ------------------------------------------------------------------ */
/* 2. Pango guard + cairo light-fill darkening                          */
/* ------------------------------------------------------------------ */

/* Non-zero while Pango is painting glyphs. Text colour must not be remapped. */
static int pango_depth = 0;

typedef void (*pl_fn)(cairo_t *, PangoLayout *);
void pango_cairo_show_layout(cairo_t *cr, PangoLayout *l) {
    static pl_fn real;
    if (!real) real = (pl_fn)dlsym(RTLD_NEXT, "pango_cairo_show_layout");
    pango_depth++; real(cr, l); pango_depth--;
}

typedef void (*pll_fn)(cairo_t *, PangoLayoutLine *);
void pango_cairo_show_layout_line(cairo_t *cr, PangoLayoutLine *l) {
    static pll_fn real;
    if (!real) real = (pll_fn)dlsym(RTLD_NEXT, "pango_cairo_show_layout_line");
    pango_depth++; real(cr, l); pango_depth--;
}

typedef void (*pgs_fn)(cairo_t *, PangoFont *, PangoGlyphString *);
void pango_cairo_show_glyph_string(cairo_t *cr, PangoFont *f, PangoGlyphString *g) {
    static pgs_fn real;
    if (!real) real = (pgs_fn)dlsym(RTLD_NEXT, "pango_cairo_show_glyph_string");
    pango_depth++; real(cr, f, g); pango_depth--;
}

typedef void (*pgi_fn)(cairo_t *, const char *, PangoGlyphItem *);
void pango_cairo_show_glyph_item(cairo_t *cr, const char *text, PangoGlyphItem *item) {
    static pgi_fn real;
    if (!real) real = (pgi_fn)dlsym(RTLD_NEXT, "pango_cairo_show_glyph_item");
    pango_depth++; real(cr, text, item); pango_depth--;
}

/* Map an opaque light grey/white fill to the dark palette.
 * Only greys are touched; coloured fills are left alone. */
static int darken_grey(double *r, double *g, double *b, double a) {
    if (pango_depth) return 0;
    if (a < 0.5) return 0;

    double mx = fmax(*r, fmax(*g, *b));
    double mn = fmin(*r, fmin(*g, *b));
    if (mx - mn > 0.03) return 0;

    if (mx >= 0.985) { *r = 0.129; *g = 0.133; *b = 0.149; return 1; } /* white   */
    if (mx >= 0.930) { *r = 0.149; *g = 0.157; *b = 0.176; return 1; } /* #F0F0F0 */
    if (mx >= 0.890) { *r = 0.180; *g = 0.188; *b = 0.207; return 1; } /* #E8E8E8 */
    if (mx >= 0.780) { *r = 0.227; *g = 0.239; *b = 0.259; return 1; } /* #CCD0C4 */
    return 0;
}

/* Recolour only when a *shape* is filled/painted, never when the source
 * colour is merely set: AnyDesk sets text colours through the same API, and
 * text must keep its original contrast. Cairo's own text routines do not call
 * the public cairo_fill(), so glyphs are never touched here. */
static int recolour_source(cairo_t *cr, double min_area, int check_area) {
    cairo_pattern_t *pat = cairo_get_source(cr);
    if (!pat || cairo_pattern_get_type(pat) != CAIRO_PATTERN_TYPE_SOLID)
        return 0;
    double r, g, b, a;
    if (cairo_pattern_get_rgba(pat, &r, &g, &b, &a) != CAIRO_STATUS_SUCCESS)
        return 0;
    double area = -1.0;
    if (check_area) {
        double x1, y1, x2, y2;
        cairo_path_extents(cr, &x1, &y1, &x2, &y2);
        area = (x2 - x1) * (y2 - y1);
        if (area < min_area)
            return 0;
    }
    double nr = r, ng = g, nb = b;
    if (!darken_grey(&nr, &ng, &nb, a))
        return 0;
    cairo_save(cr);
    cairo_set_source_rgba(cr, nr, ng, nb, a);
    return 1;
}

typedef void (*fill_fn)(cairo_t *);
void cairo_fill(cairo_t *cr) {
    static fill_fn real;
    if (!real) real = (fill_fn)dlsym(RTLD_NEXT, "cairo_fill");
    int recol = recolour_source(cr, 900.0, 1);
    real(cr);
    if (recol) cairo_restore(cr);
}

void cairo_fill_preserve(cairo_t *cr) {
    static fill_fn real;
    if (!real) real = (fill_fn)dlsym(RTLD_NEXT, "cairo_fill_preserve");
    int recol = recolour_source(cr, 900.0, 1);
    real(cr);
    if (recol) cairo_restore(cr);
}

/* Text keep-out: AnyDesk paints a few labels with a dark grey source. On a
 * dark UI they would be unreadable, so lift dark-grey *text* only. */
static int lighten_text(cairo_t *cr) {
    cairo_pattern_t *pat = cairo_get_source(cr);
    if (!pat || cairo_pattern_get_type(pat) != CAIRO_PATTERN_TYPE_SOLID)
        return 0;
    double r, g, b, a;
    if (cairo_pattern_get_rgba(pat, &r, &g, &b, &a) != CAIRO_STATUS_SUCCESS)
        return 0;
    if (a < 0.30) return 0;
    double mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b));
    if (mx - mn > 0.03) return 0;
    if (mx < 0.25 || mx > 0.60) return 0;
    cairo_save(cr);
    cairo_set_source_rgba(cr, 0.72, 0.74, 0.77, a);
    return 1;
}

typedef void (*st_fn)(cairo_t *, const char *);
void cairo_show_text(cairo_t *cr, const char *utf8) {
    static st_fn real;
    if (!real) real = (st_fn)dlsym(RTLD_NEXT, "cairo_show_text");
    int l = lighten_text(cr);
    real(cr, utf8);
    if (l) cairo_restore(cr);
}

typedef void (*sg_fn)(cairo_t *, const cairo_glyph_t *, int);
void cairo_show_glyphs(cairo_t *cr, const cairo_glyph_t *g, int n) {
    static sg_fn real;
    if (!real) real = (sg_fn)dlsym(RTLD_NEXT, "cairo_show_glyphs");
    int l = lighten_text(cr);
    real(cr, g, n);
    if (l) cairo_restore(cr);
}

typedef void (*stg_fn)(cairo_t *, const char *, int, const cairo_glyph_t *, int,
                       const cairo_text_cluster_t *, int, cairo_text_cluster_flags_t);
void cairo_show_text_glyphs(cairo_t *cr, const char *utf8, int len,
                            const cairo_glyph_t *g, int ng,
                            const cairo_text_cluster_t *cl, int ncl,
                            cairo_text_cluster_flags_t fl) {
    static stg_fn real;
    if (!real) real = (stg_fn)dlsym(RTLD_NEXT, "cairo_show_text_glyphs");
    int l = lighten_text(cr);
    real(cr, utf8, len, g, ng, cl, ncl, fl);
    if (l) cairo_restore(cr);
}

typedef void (*paint_fn)(cairo_t *);
void cairo_paint(cairo_t *cr) {
    static paint_fn real;
    if (!real) real = (paint_fn)dlsym(RTLD_NEXT, "cairo_paint");
    int recol = recolour_source(cr, 0.0, 0);
    real(cr);
    if (recol) cairo_restore(cr);
}

/* ------------------------------------------------------------------ */
/* 3. Icon pixbuf inversion (small monochrome glyphs)                   */
/* ------------------------------------------------------------------ */

typedef void (*pixbuf_fn)(cairo_t *, const GdkPixbuf *, double, double);
static GHashTable *icon_cache;

static GdkPixbuf *invert_glyph_pixbuf(const GdkPixbuf *pb) {
    if (!pb) return NULL;
    int w = gdk_pixbuf_get_width(pb);
    int h = gdk_pixbuf_get_height(pb);
    if (w <= 0 || h <= 0 || w * h > 128 * 128) return NULL;
    if (gdk_pixbuf_get_n_channels(pb) != 4) return NULL;

    int rs = gdk_pixbuf_get_rowstride(pb);
    guchar *px = gdk_pixbuf_get_pixels(pb);
    long tot = (long)w * h, dark = 0, coloured = 0;

    for (int y = 0; y < h; y++) {
        guchar *row = px + (size_t)y * rs;
        for (int x = 0; x < w; x++) {
            guchar *p = row + (size_t)x * 4;
            if (p[3] < 16) continue;
            int mx = p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]);
            int mn = p[0] < p[1] ? (p[0] < p[2] ? p[0] : p[2]) : (p[1] < p[2] ? p[1] : p[2]);
            if (mx - mn > 28) coloured++;
            if (mx < 90) dark++;
        }
    }
    if (coloured * 100 > tot * 8) return NULL;   /* keep coloured icons      */
    if (dark * 100 < tot * 35) return NULL;      /* only dark glyph masks    */

    GdkPixbuf *out = gdk_pixbuf_copy(pb);
    if (!out) return NULL;
    guchar *op = gdk_pixbuf_get_pixels(out);
    int ors = gdk_pixbuf_get_rowstride(out);

    for (int y = 0; y < h; y++) {
        guchar *row = op + (size_t)y * ors;
        for (int x = 0; x < w; x++) {
            guchar *p = row + (size_t)x * 4;
            if (p[3] < 16) continue;
            int mx = p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]);
            int mn = p[0] < p[1] ? (p[0] < p[2] ? p[0] : p[2]) : (p[1] < p[2] ? p[1] : p[2]);
            if (mx - mn > 28) continue;          /* keep colour              */
            guchar v = 255 - p[0];
            p[0] = p[1] = p[2] = v;
        }
    }
    return out;
}

void gdk_cairo_set_source_pixbuf(cairo_t *cr, const GdkPixbuf *pb, double x, double y) {
    static pixbuf_fn real;
    if (!real) real = (pixbuf_fn)dlsym(RTLD_NEXT, "gdk_cairo_set_source_pixbuf");
    if (!icon_cache)
        icon_cache = g_hash_table_new(g_direct_hash, g_direct_equal);

    const GdkPixbuf *use = pb;
    GdkPixbuf *cached = g_hash_table_lookup(icon_cache, (gpointer)pb);
    if (!cached) {
        cached = invert_glyph_pixbuf(pb);
        if (cached) {
            g_hash_table_insert(icon_cache, (gpointer)pb, cached);
        } else {
            g_hash_table_insert(icon_cache, (gpointer)pb, (gpointer)pb);
        }
    }
    if (cached && cached != pb) use = cached;
    real(cr, use, x, y);
}
