"""Ray – triangle intersection for many rays at once: a bounding volume hierarchy (built level by
level: triangles that are large for their node are set apart, the others split at the median
along the longest axis) and a traversal that advances all rays together, one node per ray and
step, nearer child first. NumPy only."""

from __future__ import annotations

import numpy as np

LEAF = 4


def _ranges(op, values: np.ndarray, start: np.ndarray, end: np.ndarray) -> np.ndarray:
    """op.reduceat over the sorted, disjoint ranges start[i]:end[i] of values (along axis 0)."""
    idx = np.empty(2 * len(start), dtype=np.int64)
    idx[0::2], idx[1::2] = start, end
    if idx[-1] >= len(values):
        idx = idx[:-1]
    return op.reduceat(values, idx, axis=0)[0::2]


def _cross(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    return np.stack([a[:, 1] * b[:, 2] - a[:, 2] * b[:, 1],
                     a[:, 2] * b[:, 0] - a[:, 0] * b[:, 2],
                     a[:, 0] * b[:, 1] - a[:, 1] * b[:, 0]], axis=1)


class BVH:
    def __init__(self, v0: np.ndarray, e1: np.ndarray, e2: np.ndarray):
        n = len(v0)
        if n == 0:
            raise ValueError("no triangles")
        v1, v2 = v0 + e1, v0 + e2
        lo = np.minimum(np.minimum(v0, v1), v2)
        hi = np.maximum(np.maximum(v0, v1), v2)
        centre = (v0 + v1 + v2) / 3.0
        size = (hi - lo).max(axis=1)
        order = np.arange(n)
        start, end, left, right = [0], [n], [-1], [-1]
        bmin, bmax = [lo.min(axis=0)], [hi.max(axis=0)]
        level = np.array([0])
        depth = 1
        while level.size:
            s = np.array([start[i] for i in level])
            e = np.array([end[i] for i in level])
            split = (e - s) > LEAF
            if not split.any():
                break
            ids, s, e = level[split], s[split], e[split]
            c = centre[order]
            extent = _ranges(np.maximum, c, s, e) - _ranges(np.minimum, c, s, e)
            ax = np.argmax(extent, axis=1)
            lens = e - s
            label = np.repeat(np.arange(len(ids)), lens)
            pos = np.repeat(s - np.concatenate([[0], np.cumsum(lens)[:-1]]), lens) + np.arange(lens.sum())
            # triangles that are large for their node (a wall among small parts) go to a child of
            # their own: their boxes would otherwise cover the small ones' and every ray would
            # have to look into both
            node_size = (np.asarray([bmax[i] for i in ids]) - np.asarray([bmin[i] for i in ids])).max(axis=1)
            big = (size[order[pos]] > 0.4 * node_size[label]).astype(np.int64)
            n_big = np.bincount(label, weights=big, minlength=len(ids)).astype(np.int64)
            by_size = (n_big > 0) & (n_big < lens) & (depth < 48)
            big = big * by_size[label]
            perm = np.lexsort((c[pos, ax[label]], big, label))
            order[pos] = order[pos][perm]
            mid = np.where(by_size, e - n_big, s + lens // 2)
            cs = np.column_stack([s, mid]).ravel()          # children, left then right, in order
            ce = np.column_stack([mid, e]).ravel()
            first = len(start)
            child = first + np.arange(2 * len(ids))
            cmin = _ranges(np.minimum, lo[order], cs, ce)
            cmax = _ranges(np.maximum, hi[order], cs, ce)
            for k, i in enumerate(ids):
                left[i], right[i] = first + 2 * k, first + 2 * k + 1
            start.extend(cs.tolist())
            end.extend(ce.tolist())
            left.extend([-1] * len(child))
            right.extend([-1] * len(child))
            bmin.extend(cmin)
            bmax.extend(cmax)
            level = child
            depth += 1
        self.order = order                                  # triangle of slot k: order[k]
        self.v0, self.e1, self.e2 = v0[order], e1[order], e2[order]
        self.start = np.asarray(start, dtype=np.int64)
        self.count = np.asarray(end, dtype=np.int64) - self.start
        self.left = np.asarray(left, dtype=np.int64)
        self.right = np.asarray(right, dtype=np.int64)
        self.children = np.column_stack([self.left, self.right])
        self.bmin = np.asarray(bmin, dtype=float)
        self.bmax = np.asarray(bmax, dtype=float)
        self.depth = depth
        self.leaf = int(self.count[self.left < 0].max())

    def intersect(self, o: np.ndarray, d: np.ndarray, tmax=None, *, any_hit: bool = False, accept=None):
        """Nearest hit of the rays o + t d with 0 < t < tmax: (t, triangle, b1, b2) with triangle -1
        and t = inf for no hit; the hit point is v0 + b1 e1 + b2 e2. `accept(ray, triangle, b1, b2)`
        can refuse hits (trimming); `ray` is the index of the ray. With `any_hit` a ray stops at
        its first accepted hit (which need not be the nearest) – for visibility."""
        n = len(o)
        t_best = np.full(n, np.inf) if tmax is None else np.array(np.broadcast_to(tmax, (n,)), dtype=float)
        tri = np.full(n, -1, dtype=np.int64)
        hb1, hb2 = np.zeros(n), np.zeros(n)
        if n == 0:
            return t_best, tri, hb1, hb2
        with np.errstate(divide="ignore"):
            inv = np.where(np.abs(d) > 1e-300, 1.0 / d, 1e300)
        # working set: the rays still under way (ids), their data side by side; a ray's stack holds
        # the nodes whose boxes it enters, with the entry distance
        ids = np.arange(n)
        lo, li, ld, best = o, inv, d, t_best.copy()
        stack = np.zeros((n, self.depth + 2), dtype=np.int64)
        entry = np.zeros((n, self.depth + 2))
        sp = np.ones(n, dtype=np.int64)
        while True:
            act = np.nonzero(sp > 0)[0]
            if not act.size:
                break
            if act.size < 0.7 * len(ids):                    # drop the finished rays
                ids, lo, li, ld, best = ids[act], lo[act], li[act], ld[act], best[act]
                stack, entry, sp = stack[act], entry[act], sp[act]
                act = np.arange(len(ids))
            top = sp[act] - 1
            node = stack[act, top]
            sp[act] = top
            live = entry[act, top] < best[act]               # a nearer hit was found meanwhile
            act, node = act[live], node[live]
            leaf = self.left[node] < 0
            ri, ni = act[~leaf], node[~leaf]
            if ri.size:                                      # inner nodes: the boxes of both children
                ch = self.children[ni]
                oo, ii = lo[ri][:, None, :], li[ri][:, None, :]
                t0, t1 = (self.bmin[ch] - oo) * ii, (self.bmax[ch] - oo) * ii
                near = np.maximum(np.minimum(t0, t1).max(axis=2), 0.0)
                far = np.maximum(t0, t1).min(axis=2)
                hit = (far >= near) & (near < best[ri][:, None])
                k = np.arange(len(ri))
                first = (near[:, 0] < near[:, 1]).astype(np.int64)      # pushed first: the farther child
                top = sp[ri]
                for c in (first, 1 - first):
                    h = hit[k, c]
                    rh, th = ri[h], top[h]
                    stack[rh, th] = ch[k, c][h]
                    entry[rh, th] = near[k, c][h]
                    top = top + h
                sp[ri] = top
            rl, nl = act[leaf], node[leaf]
            if rl.size:
                s0, cnt = self.start[nl], self.count[nl]
                for k in range(self.leaf):
                    m = cnt > k
                    if not m.any():
                        break
                    r, slot = (rl, s0 + k) if m.all() else (rl[m], s0[m] + k)
                    e1, e2, dr = self.e1[slot], self.e2[slot], ld[r]
                    p = _cross(dr, e2)
                    det = np.einsum("ij,ij->i", e1, p)
                    with np.errstate(divide="ignore", invalid="ignore"):
                        idet = 1.0 / det
                        sv = lo[r] - self.v0[slot]
                        b1 = np.einsum("ij,ij->i", sv, p) * idet
                        q = _cross(sv, e1)
                        b2 = np.einsum("ij,ij->i", dr, q) * idet
                        t = np.einsum("ij,ij->i", e2, q) * idet
                        hit = (det != 0) & (b1 >= -1e-12) & (b2 >= -1e-12) & (b1 + b2 <= 1 + 1e-12) \
                            & (t > 0) & (t < best[r])
                    if not hit.any():
                        continue
                    r, slot, b1, b2, t = r[hit], slot[hit], b1[hit], b2[hit], t[hit]
                    if accept is not None:
                        good = accept(ids[r], self.order[slot], b1, b2)
                        r, slot, b1, b2, t = r[good], slot[good], b1[good], b2[good], t[good]
                    best[r] = t
                    g = ids[r]
                    t_best[g], tri[g], hb1[g], hb2[g] = t, self.order[slot], b1, b2
                    if any_hit:
                        sp[r] = 0
        t_best[tri < 0] = np.inf
        return t_best, tri, hb1, hb2

    def refresh(self, v0: np.ndarray, e1: np.ndarray, e2: np.ndarray) -> None:
        """Takes over triangles whose vertex order changed (same triangles, same boxes)."""
        self.v0, self.e1, self.e2 = v0[self.order], e1[self.order], e2[self.order]

    def occluded(self, o: np.ndarray, d: np.ndarray, *, accept=None) -> np.ndarray:
        """Whether something lies on the segment o + t d, 0 < t < 1."""
        return self.intersect(o, d, 1.0, any_hit=True, accept=accept)[1] >= 0
