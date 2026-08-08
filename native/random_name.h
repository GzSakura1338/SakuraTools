#pragma once

#include <string>

std::string GenerateRandomClassName(int minWords = 2, int maxWords = 3);

std::string MakeInternalName(const std::string& internalPackage,
                             const std::string& simpleName);

const std::string& GetTrampolinePackage();
