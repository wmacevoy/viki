"""vkembed.py -- the host's embedder for the vector method (REQ3/Q-6), over src/'s embed.c.

Built by tools/build-embed.sh as core/build-store/libviki_embed.so (or $VIKI_EMBED_LIB). The model directory
holds model.onnx, vocab.txt and viki-manifest.json, as src/ pinned them (build/versions.env). A model is named
by its bytes: the manifest's id plus the first 16 hex of each file's sha256, so two hosts with the same files
agree and a changed file is a different model (Q-6).
"""
import array
import ctypes as C
import hashlib
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def _lib():
    path = os.environ.get("VIKI_EMBED_LIB") or os.path.join(HERE, "..", "core", "build-store", "libviki_embed.so")
    lib = C.CDLL(path)
    lib.viki_embedder_open.argtypes, lib.viki_embedder_open.restype = [C.c_char_p], C.c_void_p
    lib.viki_embedder_close.argtypes, lib.viki_embedder_close.restype = [C.c_void_p], None
    lib.viki_embedder_dim.argtypes, lib.viki_embedder_dim.restype = [C.c_void_p], C.c_int
    lib.viki_embed.argtypes, lib.viki_embed.restype = [C.c_void_p, C.c_char_p, C.POINTER(C.c_float)], C.c_int
    return lib


def _sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


class Embedder:
    def __init__(self, model_dir):
        with open(os.path.join(model_dir, "viki-manifest.json")) as f:
            manifest = json.load(f)
        msha, vsha = _sha(os.path.join(model_dir, "model.onnx")), _sha(os.path.join(model_dir, "vocab.txt"))
        # the manifest pins both hashes: a file that does not match it is refused, not used
        if msha != manifest.get("model_sha256") or vsha != manifest.get("vocab_sha256"):
            raise ValueError(f"{model_dir}: model.onnx or vocab.txt does not match viki-manifest.json")
        self.lib = _lib()
        self.e = self.lib.viki_embedder_open(model_dir.encode())
        if not self.e:
            raise ValueError(f"{model_dir}: the embedder did not open")
        self.dim = self.lib.viki_embedder_dim(self.e)
        self.id = f"{manifest['model_id']}@{msha[:16]}.{vsha[:16]}"
        self._out = (C.c_float * self.dim)()

    def embed(self, text):
        """An L2-normalized vector of text (truncated by the model at 256 wordpieces), as array('f')."""
        if self.lib.viki_embed(self.e, text.encode("utf-8", "replace"), self._out):
            raise ValueError("the embedder failed")
        return array.array("f", self._out)

    def close(self):
        if self.e:
            self.lib.viki_embedder_close(self.e)
            self.e = None
