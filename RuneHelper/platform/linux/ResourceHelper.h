#pragma once

#include <string>
#include <string_view>

std::string_view EmbeddedTextModel(std::string_view language);
std::string_view EmbeddedImage(std::string_view file);
std::string LoadEmbeddedRecipeDatabase();
