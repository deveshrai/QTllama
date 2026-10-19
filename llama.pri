# Links against the llama.cpp static libraries produced by scripts/build_llama.sh.
# Override the location with:  qmake LLAMA_PREFIX=/path/to/install

isEmpty(LLAMA_PREFIX): LLAMA_PREFIX = $$PWD/third_party/llama-install
LLAMA_LIBDIR = $$LLAMA_PREFIX/lib

!exists($$LLAMA_PREFIX/include/llama.h) {
    error("llama.cpp not found in $$LLAMA_PREFIX — run scripts/build_llama.sh first")
}

INCLUDEPATH += $$LLAMA_PREFIX/include

# Static link order matters: llama -> ggml -> backends -> ggml-base.
LIBS += -L$$LLAMA_LIBDIR -lllama -lggml

# Optional backends: link whichever ones the build script produced.
exists($$LLAMA_LIBDIR/libggml-metal.a) {
    LIBS += -lggml-metal
    LIBS += -framework Metal -framework MetalKit -framework Foundation
}
exists($$LLAMA_LIBDIR/libggml-blas.a) {
    LIBS += -lggml-blas
    macx: LIBS += -framework Accelerate
    else: LIBS += -lopenblas
}
exists($$LLAMA_LIBDIR/libggml-vulkan.a): LIBS += -lggml-vulkan -lvulkan
exists($$LLAMA_LIBDIR/libggml-cuda.a):   LIBS += -lggml-cuda -lcudart -lcublas -lcuda

LIBS += -lggml-cpu -lggml-base

macx: LIBS += -framework Accelerate
unix:!macx: LIBS += -lpthread -ldl -lm

# Rebuild apps when llama.cpp is rebuilt.
PRE_TARGETDEPS += $$LLAMA_LIBDIR/libllama.a
