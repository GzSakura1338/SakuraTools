#pragma once
#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Models the scoreboard sent to B, not A's asynchronously updated scoreboard.
class TeamState {
  public:
    using Name = std::u16string;
    struct Update {
        bool send = false;
        std::vector<Name> players;
    };

    Update apply(const Name& team, int method, const std::vector<Name>& players, const Name& a, const Name& b) {
        Update result;
        if (method < 0 || method > 4 || (method != 0 && !teams_.count(team)))
            return result;
        result.send = true;
        if (method == 0)
            teams_.insert(team);
        if (method == 1) {
            teams_.erase(team);
            for (auto it = members_.begin(); it != members_.end();) {
                if (it->second == team)
                    it = members_.erase(it);
                else
                    ++it;
            }
            return result;
        }
        if (method == 2)
            return result;

        auto candidates = players;
        if (!a.empty() && !b.empty() && a != b) {
            // B's local identity follows A, even if the remote roster contains B's name.
            candidates.erase(std::remove(candidates.begin(), candidates.end(), b), candidates.end());
            if (std::find(players.begin(), players.end(), a) != players.end())
                candidates.push_back(b);
        }
        std::unordered_set<Name> seen;
        for (const auto& player : candidates) {
            if (!seen.insert(player).second)
                continue;
            auto member = members_.find(player);
            if (method == 4) {
                if (member == members_.end() || member->second != team)
                    continue;
                members_.erase(member);
            } else {
                members_[player] = team;
            }
            result.players.push_back(player);
        }
        if ((method == 3 || method == 4) && result.players.empty())
            result.send = false;
        return result;
    }

  private:
    std::unordered_set<Name> teams_;
    std::unordered_map<Name, Name> members_;
};
