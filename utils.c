#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "utils.h"

// Default parameters based on Python implementation
static const int DEFAULT_FACTORS[MAX_PYRAMID_LEVELS] = {1, 2, 4, 4};
static const int DEFAULT_SEARCH_RADII[MAX_PYRAMID_LEVELS] = {1, 4, 4, 4};
static const bool DEFAULT_USE_L1[MAX_PYRAMID_LEVELS] = {true, false, false, false};

Image* load_image(const char* filename) {
    int width, height, channels;
    unsigned char* data = stbi_load(filename, &width, &height, &channels, 0);
    if (!data) {
        fprintf(stderr, "Error loading image %s\n", filename);
        return NULL;
    }

    // Create image structure with proper number of channels
    Image* img = create_image(height, width, channels);
    if (!img) {
        stbi_image_free(data);
        return NULL;
    }

    // Convert to floating point and normalize to [0,1]
    for (int i = 0; i < height * width; i++) {
        for (int c = 0; c < channels; c++) {
            img->data[i * channels + c] = (float)data[i * channels + c] / 255.0f;
        }
    }

    stbi_image_free(data);
    return img;
}

bool save_image(const char* filename, const Image* img) {
    if (!img || !img->data) return false;

    // Convert to 8-bit, accounting for all channels
    unsigned char* data = (unsigned char*)malloc(img->height * img->width * img->channels);
    if (!data) return false;

    for (int i = 0; i < img->height * img->width * img->channels; i++) {
        float val = img->data[i];
        val = val < 0.0f ? 0.0f : (val > 1.0f ? 1.0f : val);
        data[i] = (unsigned char)(val * 255.0f);
    }

    bool success = stbi_write_png(filename, img->width, img->height, img->channels, data, img->width * img->channels);
    free(data);
    return success;
}

Image* create_grayscale(const Image* color_img) {
    if (!color_img || !color_img->data) return NULL;

    Image* gray = create_image(color_img->height, color_img->width, color_img->channels);
    if (!gray) return NULL;

    // Copy data (assuming input is already grayscale)
    memcpy(gray->data, color_img->data, sizeof(pixel_t) * color_img->height * color_img->width);
    return gray;
}

AlignmentParams* create_default_params(void) {
    AlignmentParams* params = (AlignmentParams*)malloc(sizeof(AlignmentParams));
    if (!params) return NULL;

    params->num_pyramid_levels = MAX_PYRAMID_LEVELS;
    
    // Allocate and initialize arrays
    params->factors = (int*)malloc(sizeof(int) * MAX_PYRAMID_LEVELS);
    params->tile_sizes = (int*)malloc(sizeof(int) * MAX_PYRAMID_LEVELS);
    params->search_radii = (int*)malloc(sizeof(int) * MAX_PYRAMID_LEVELS);
    params->use_l1_dist = (bool*)malloc(sizeof(bool) * MAX_PYRAMID_LEVELS);

    if (!params->factors || !params->tile_sizes || 
        !params->search_radii || !params->use_l1_dist) {
        free_alignment_params(params);
        return NULL;
    }

    // Copy default values
    memcpy(params->factors, DEFAULT_FACTORS, sizeof(int) * MAX_PYRAMID_LEVELS);
    memcpy(params->search_radii, DEFAULT_SEARCH_RADII, sizeof(int) * MAX_PYRAMID_LEVELS);
    memcpy(params->use_l1_dist, DEFAULT_USE_L1, sizeof(bool) * MAX_PYRAMID_LEVELS);

    // Set tile sizes based on base tile size
    params->tile_size = DEFAULT_TILE_SIZE;
    for (int i = 0; i < MAX_PYRAMID_LEVELS; i++) {
        params->tile_sizes[i] = (i == MAX_PYRAMID_LEVELS - 1) ? 
                               params->tile_size / 2 : params->tile_size;
    }

    // ICA parameters
    params->sigma_blur = 0.0f;
    params->num_iterations = 3;

    return params;
}

void free_alignment_params(AlignmentParams* params) {
    if (params) {
        free(params->factors);
        free(params->tile_sizes);
        free(params->search_radii);
        free(params->use_l1_dist);
        free(params);
    }
}

void print_progress(const char* message, int verbose_level) {
    if (verbose_level > 0) {
        printf("%s\n", message);
    }
}

double get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void clip_value(float* value, float min, float max) {
    if (*value < min) *value = min;
    else if (*value > max) *value = max;
}
