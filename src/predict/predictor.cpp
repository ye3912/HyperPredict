#include "predict/predictor.h"
#include <ctime>

namespace hp::predict {

float FTRL::predict(const std::array<float, DIM>& x) const noexcept {
    float wx = 0.f;
    #pragma unroll
    for(size_t i = 0; i < DIM; ++i) wx += w_[i] * x[i];
    return 1.f / (1.f + std::exp(-std::clamp(wx, -3.f, 3.f)));
}

void FTRL::upd(size_t i, float g, float s) noexcept {
    float ow = w_[i];
    float at = ALPHA / (BETA + std::sqrt(n_[i]));
    z_[i] += g - s * ow;
    if(std::abs(z_[i]) <= L1) {
        w_[i] = 0.f;
    } else {
        float sg = (z_[i] > 0.f) ? -1.f : 1.f;
        w_[i] = -(z_[i] - sg * L1) / (L2 + (BETA + std::sqrt(n_[i])) / at);
    }
    n_[i] += g * g;
}

void FTRL::update(const std::array<float, DIM>& x, bool label) noexcept {
    float p = predict(x);
    float g = p - static_cast<float>(label);
    float s = (1.f / ALPHA) * (std::sqrt(1.f + n_[0]) - 1.f);
    for(size_t i = 0; i < DIM; ++i) {
        if(std::abs(x[i]) > 1e-5f) upd(i, g * x[i], s);
    }
}

bool FTRL::save_bin(const char* path) const noexcept {
    FILE* f = fopen(path, "wb");
    if(!f) return false;
    const char magic[] = "HP_MDL";
    fwrite(magic, 1, 6, f);
    uint32_t version = 1;
    fwrite(&version, sizeof(version), 1, f);
    fwrite(w_.data(), sizeof(float), DIM, f);
    fwrite(z_.data(), sizeof(float), DIM, f);
    fwrite(n_.data(), sizeof(float), DIM, f);
    fclose(f);
    return true;
}

bool FTRL::load_bin(const char* path) noexcept {
    FILE* f = fopen(path, "rb");    if(!f) return false;
    char magic[7] = {0};
    fread(magic, 1, 6, f);
    if(std::strcmp(magic, "HP_MDL") != 0) { fclose(f); return false; }
    uint32_t version = 0;
    fread(&version, sizeof(version), 1, f);
    if(version != 1) { fclose(f); return false; }
    size_t r1 = fread(w_.data(), sizeof(float), DIM, f);
    size_t r2 = fread(z_.data(), sizeof(float), DIM, f);
    size_t r3 = fread(n_.data(), sizeof(float), DIM, f);
    fclose(f);
    return (r1 == DIM && r2 == DIM && r3 == DIM);
}

bool FTRL::export_json(const char* path) const noexcept {
    FILE* f = fopen(path, "w");
    if(!f) return false;
    
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", t);
    
    fprintf(f, "{\n");
    fprintf(f, "  \"meta\": {\n");
    fprintf(f, "    \"algorithm\": \"FTRL-Proximal\",\n");
    fprintf(f, "    \"version\": 1,\n");
    fprintf(f, "    \"export_time\": \"%s\",\n", timebuf);
    fprintf(f, "    \"dimensions\": %zu,\n", DIM);
    fprintf(f, "    \"hyperparameters\": {\n");
    fprintf(f, "      \"alpha\": %.4f,\n", ALPHA);
    fprintf(f, "      \"beta\": %.4f,\n", BETA);
    fprintf(f, "      \"l1\": %.6f,\n", L1);
    fprintf(f, "      \"l2\": %.6f\n", L2);
    fprintf(f, "    }\n");
    fprintf(f, "  },\n");
    
    fprintf(f, "  \"weights\": [");
    for(size_t i = 0; i < DIM; ++i) {
        fprintf(f, "\n    %.8f%s", w_[i], i < DIM - 1 ? "," : "");
    }
    fprintf(f, "\n  ],\n");
    
    fprintf(f, "  \"z_accumulator\": [");
    for(size_t i = 0; i < DIM; ++i) {
        fprintf(f, "\n    %.8f%s", z_[i], i < DIM - 1 ? "," : "");
    }
    fprintf(f, "\n  ],\n");
    
    fprintf(f, "  \"n_squared_sum\": [");    for(size_t i = 0; i < DIM; ++i) {
        fprintf(f, "\n    %.8f%s", n_[i], i < DIM - 1 ? "," : "");
    }
    fprintf(f, "\n  ]\n");
    
    fprintf(f, "}\n");
    fclose(f);
    return true;
}

} // namespace hp::predict