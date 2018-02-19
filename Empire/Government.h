#ifndef _Government_h_
#define _Government_h_

#include "../util/Export.h"

#include <boost/serialization/access.hpp>

#include <map>
#include <set>
#include <string>

FO_COMMON_API extern const int ALL_EMPIRES;

class FO_COMMON_API Policy {
public:

};

/** Keeps track of policies that can be chosen by empires. */
class FO_COMMON_API PoliciesManager {
public:
    /** \name Structors */ //@{
    PoliciesManager();
    PoliciesManager& operator=(const PoliciesManager& rhs);
    //@}

    /** \name Accessors */ //@{
    /** Returns set of directed starlane traversals along which supply can flow.
      * Results are pairs of system ids of start and end system of traversal. */
    const std::map<int, std::set<std::pair<int, int>>>&     SupplyStarlaneTraversals() const;

    std::string Dump(int empire_id = ALL_EMPIRES) const;
    //@}

    /** \name Mutators */ //@{
    /** Calculates systems at which fleets of empires can be supplied, and
      * groups of systems that can exchange resources, and the starlane
      * traversals used to do so. */
    void    Update();
    //@}

private:
    /** ordered pairs of system ids between which a starlane runs that can be
        used to convey resources between systems. indexed first by empire id. */
    std::map<std::string, Policy> m_policies;


    friend class boost::serialization::access;
    template <class Archive>
    void serialize(Archive& ar, const unsigned int version);
};

#endif // _Government_h_
