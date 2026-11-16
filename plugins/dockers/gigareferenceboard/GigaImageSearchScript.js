/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *
 *  Giga Sketch: injected into every page of the Reference Board's image
 *  search browser. It runs in its own "world" (the page's scripts can't see
 *  it, it can't see theirs; both see the same page elements).
 *
 *  - gigaCandidates(img): image addresses to try, best (largest) first.
 *    The C++ side downloads the first one that works.
 *  - Pick mode: clicks on images mark them instead of opening them.
 *  - Dragging an image carries its best address (alternatives packed into
 *    the #giga-alt= part, which servers never see).
 *
 *  Messages to C++ go through console.log("giga:...") (no extra Qt module needed).
 */
(function () {
    if (window.__giga) return;

    function abs(u) {
        try { return new URL(u, document.baseURI).href; } catch (e) { return ""; }
    }
    function usable(u) {
        return u && (u.startsWith("http:") || u.startsWith("https:") || u.startsWith("data:image/") || u.startsWith("file:"));
    }

    // Biggest entry of a srcset ("a.jpg 320w, b.jpg 1280w" or "... 2x").
    function biggestFromSrcset(srcset) {
        if (!srcset) return "";
        let best = "", bestScore = -1;
        for (const part of srcset.split(",")) {
            const bits = part.trim().split(/\s+/);
            if (!bits[0]) continue;
            let score = 1;
            const d = bits[1] || "";
            if (d.endsWith("w")) score = parseFloat(d);
            else if (d.endsWith("x")) score = parseFloat(d) * 1000;
            if (score > bestScore) { bestScore = score; best = bits[0]; }
        }
        return abs(best);
    }

    // Pinterest serves sizes in the path: /236x/, /474x/, /736x/, /originals/.
    function pinterestBigger(u) {
        const m = u.match(/^(https?:\/\/i\.pinimg\.com\/)(\d+x|originals)(\/.*)$/);
        if (!m) return [];
        const out = [m[1] + "originals" + m[3], m[1] + "736x" + m[3]];
        // "originals" are sometimes .png when the small one is .jpg
        if (/\.jpe?g$/i.test(m[3])) out.splice(1, 0, m[1] + "originals" + m[3].replace(/\.jpe?g$/i, ".png"));
        return out;
    }

    // Older/other Google and Bing result links carry the original as a parameter.
    function fromLink(a) {
        if (!a || !a.href) return [];
        const out = [];
        try {
            const url = new URL(a.href, document.baseURI);
            for (const key of ["imgurl", "mediaurl", "url"]) {
                const v = url.searchParams.get(key);
                if (v && /^https?:/.test(v) && (key !== "url" || /\.(jpe?g|png|webp|gif|bmp|avif)(\?|$)/i.test(v))) out.push(v);
            }
            if (/\.(jpe?g|png|webp|gif|bmp|avif)(\?|$)/i.test(url.pathname)) out.push(url.href);
        } catch (e) {}
        return out;
    }

    // Bing: the result tile <a class="iusc" m='{"murl": "...original...", "turl": "...thumb..."}'>.
    function fromBing(img) {
        const tile = img.closest("[m]");
        if (!tile) return [];
        try {
            const m = JSON.parse(tile.getAttribute("m"));
            const out = [];
            if (m.murl) out.push(m.murl);
            if (m.turl) out.push(m.turl);
            return out;
        } catch (e) { return []; }
    }

    function gigaCandidates(img) {
        if (!img) return [];
        const shown = abs(img.currentSrc || img.src || "");
        let list = [];
        list = list.concat(fromBing(img));
        list = list.concat(fromLink(img.closest("a")));
        list.push(biggestFromSrcset(img.getAttribute("srcset")));
        list = list.concat(pinterestBigger(shown));
        // Lazy-loading sites keep the real address in a data attribute.
        for (const attr of ["data-src", "data-iurl", "data-original"]) list.push(abs(img.getAttribute(attr) || ""));
        list.push(shown); // always last: the image as you see it
        const seen = new Set();
        return list.filter(u => usable(u) && !seen.has(u) && seen.add(u));
    }

    // The image under / inside an element (a result tile is often a div over an img).
    function imageOf(el) {
        if (!el) return null;
        if (el.tagName === "IMG") return el;
        const inside = el.querySelector && el.querySelector("img");
        if (inside) return inside;
        let p = el;
        for (let i = 0; i < 4 && p; ++i, p = p.parentElement) {
            const found = p.querySelector && p.querySelector("img");
            if (found) return found;
        }
        return null;
    }

    function imageAt(x, y) {
        for (const el of document.elementsFromPoint(x, y)) {
            if (el.tagName === "IMG") return el;
        }
        return imageOf(document.elementFromPoint(x, y));
    }

    // ---- Pick mode ---------------------------------------------------------
    const picked = new Map(); // img -> candidates
    let pickOn = false;
    let accent = "#3d8bfd";

    function report() { console.log("giga:pick-count:" + picked.size); }

    function mark(img, on) {
        if (on) {
            img.dataset.gigaOutline = img.style.outline;
            img.style.outline = "4px solid " + accent;
            img.style.outlineOffset = "-4px";
        } else {
            img.style.outline = img.dataset.gigaOutline || "";
            img.style.outlineOffset = "";
            delete img.dataset.gigaOutline;
        }
    }

    function toggle(img) {
        if (picked.has(img)) { picked.delete(img); mark(img, false); }
        else { picked.set(img, gigaCandidates(img)); mark(img, true); }
        report();
    }

    // Capture phase, so the site never sees the click (no result opens).
    function swallow(e) {
        if (!pickOn || e.button > 0) return;
        const img = imageOf(e.target);
        if (!img) return;
        e.preventDefault();
        e.stopImmediatePropagation();
        if (e.type === "click") toggle(img);
    }
    for (const type of ["pointerdown", "mousedown", "pointerup", "mouseup", "click", "auxclick"]) {
        window.addEventListener(type, swallow, true);
    }

    // ---- Drag: carry the best address -------------------------------------
    window.addEventListener("dragstart", function (e) {
        const img = imageOf(e.target);
        if (!img || !e.dataTransfer) return;
        const c = gigaCandidates(img);
        if (!c.length) return;
        let url = c[0];
        if (c.length > 1 && !url.startsWith("data:")) {
            url = url.split("#")[0] + "#giga-alt=" + encodeURIComponent(JSON.stringify(c.slice(1)));
        }
        e.dataTransfer.setData("text/uri-list", url);
        e.dataTransfer.setData("text/plain", url);
    }, true);

    window.__giga = {
        candidatesAt: (x, y) => gigaCandidates(imageAt(x, y)),
        candidatesOf: (i) => gigaCandidates(document.images[i]),
        candidatesForUrl: (u) => pinterestBigger(u),
        setPick: (on, color) => {
            pickOn = !!on;
            if (color) accent = color;
            document.documentElement.style.cursor = pickOn ? "copy" : "";
        },
        picked: () => Array.from(picked.values()),
        clearPicked: () => { for (const img of picked.keys()) mark(img, false); picked.clear(); report(); },
        togglePickOf: (i) => { if (document.images[i]) toggle(document.images[i]); },
    };
    report();
})();
