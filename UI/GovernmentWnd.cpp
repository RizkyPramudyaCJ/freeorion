#include "GovernmentWnd.h"

#include "ClientUI.h"
#include "CUIWnd.h"
#include "CUIControls.h"
#include "IconTextBrowseWnd.h"
#include "TextBrowseWnd.h"
#include "../parse/Parse.h"
#include "../util/i18n.h"
#include "../util/Logger.h"
#include "../util/Order.h"
#include "../util/OptionsDB.h"
#include "../util/ScopedTimer.h"
#include "../util/Directories.h"
#include "../Empire/Empire.h"
#include "../Empire/Government.h"
#include "../client/human/HumanClientApp.h"

#include <GG/DrawUtil.h>
#include <GG/StaticGraphic.h>
#include <GG/TabWnd.h>

namespace {
    const std::string   POLIVY_CONTROL_DROP_TYPE_STRING = "Policy Control";
    const std::string   EMPTY_STRING = "";
    const std::string   DES_MAIN_WND_NAME = "government.edit";
    const std::string   DES_PART_PALETTE_WND_NAME = "government.policys";
    const GG::X         POLICY_CONTROL_WIDTH(54);
    const GG::Y         POLICY_CONTROL_HEIGHT(54);
    const GG::X         SLOT_CONTROL_WIDTH(60);
    const GG::Y         SLOT_CONTROL_HEIGHT(60);
    const int           PAD(3);

    /** Returns texture with which to render a SlotControl, depending on \a slot_type. */
    std::shared_ptr<GG::Texture> SlotBackgroundTexture(const std::string& category) {
        return ClientUI::GetTexture(ClientUI::ArtDir() / "misc" / "missing.png", true);
    }

    /** Returns background texture with which to render a PolicyControl, depending on the
      * types of slot that the indicated \a policy can be put into. */
    std::shared_ptr<GG::Texture> PolicyBackgroundTexture(const Policy* policy) {
        if (policy) {
            bool social = policy->Category() == "ECONOMIC_CATEGORY";
            bool economic = policy->Category() == "SOCIAL_CATEGORY";

            return ClientUI::GetTexture(ClientUI::ArtDir() / "icons" / "ship_policys" / "core_policy.png", true);
        }
        return ClientUI::GetTexture(ClientUI::ArtDir() / "misc" / "missing.png", true);
    }
}

//////////////////////////////////////////////////
// PolicyControl                                //
//////////////////////////////////////////////////
/** UI representation of a government policy.  Displayed in the PolicyPalette,
  * and can be dragged onto SlotControls to add policies to the government. */
class PolicyControl : public GG::Control {
public:
    /** \name Structors */ //@{
    PolicyControl(const Policy* policy);
    //@}
    void CompleteConstruction() override;

    /** \name Accessors */ //@{
    const std::string&  PolicyName() const  { return m_policy ? m_policy->Name() : EMPTY_STRING; }
    const Policy*       Policy() const      { return m_policy; }
    //@}

    /** \name Mutators */ //@{
    void Render() override;
    void LClick(const GG::Pt& pt, GG::Flags<GG::ModKey> mod_keys) override;
    void LDoubleClick(const GG::Pt& pt, GG::Flags<GG::ModKey> mod_keys) override;
    void RClick(const GG::Pt& pt, GG::Flags<GG::ModKey> mod_keys) override;type);
    //@}

    mutable boost::signals2::signal<void (const Policy*, GG::Flags<GG::ModKey>)> ClickedSignal;
    mutable boost::signals2::signal<void (const Policy*, const GG::Pt& pt)> RightClickedSignal;
    mutable boost::signals2::signal<void (const Policy*)> DoubleClickedSignal;

private:
    std::shared_ptr<GG::StaticGraphic>  m_icon;
    std::shared_ptr<GG::StaticGraphic>  m_background;
    const Policy*                       m_policy;
};

PolicyControl::PolicyControl(const Policy* policy) :
    GG::Control(GG::X0, GG::Y0, SLOT_CONTROL_WIDTH, SLOT_CONTROL_HEIGHT, GG::INTERACTIVE),
    m_icon(nullptr),
    m_background(nullptr),
    m_policy(policy)
{}

void PolicyControl::CompleteConstruction() {
    GG::Control::CompleteConstruction();
    if (!m_policy)
        return;

    m_background = GG::Wnd::Create<GG::StaticGraphic>(PolicyBackgroundTexture(m_policy), GG::GRAPHIC_FITGRAPHIC | GG::GRAPHIC_PROPSCALE);
    m_background->Resize(GG::Pt(SLOT_CONTROL_WIDTH, SLOT_CONTROL_HEIGHT));
    m_background->Show();
    AttachChild(m_background);


    // position of policy image centred within policy control.  control is size of a slot, but the
    // policy image is smaller
    GG::X policy_left = (Width() - PART_CONTROL_WIDTH) / 2;
    GG::Y policy_top = (Height() - PART_CONTROL_HEIGHT) / 2;

    //DebugLogger() << "PolicyControl::PolicyControl this: " << this << " policy: " << policy << " named: " << (policy ? policy->Name() : "no policy");
    m_icon = GG::Wnd::Create<GG::StaticGraphic>(ClientUI::PolicyIcon(m_policy->Name()), GG::GRAPHIC_FITGRAPHIC | GG::GRAPHIC_PROPSCALE);
    m_icon->MoveTo(GG::Pt(policy_left, policy_top));
    m_icon->Resize(GG::Pt(PART_CONTROL_WIDTH, PART_CONTROL_HEIGHT));
    m_icon->Show();
    AttachChild(m_icon);

    SetDragDropDataType(POLICY_CONTROL_DROP_TYPE_STRING);

    //DebugLogger() << "PolicyControl::PolicyControl policy name: " << m_policy->Name();
    SetBrowseModeTime(GetOptionsDB().Get<int>("ui.tooltip.delay"));
    SetBrowseInfoWnd(GG::Wnd::Create<IconTextBrowseWnd>(
        ClientUI::PolicyIcon(m_policy->Name()),
        UserString(m_policy->Name()),
        UserString(m_policy->Description()) + "\n" + m_policy->CapacityDescription()
    ));
}

void PolicyControl::Render() {}

void PolicyControl::LClick(const GG::Pt& pt, GG::Flags<GG::ModKey> mod_keys)
{ ClickedSignal(m_policy, mod_keys); }

void PolicyControl::LDoubleClick(const GG::Pt& pt, GG::Flags<GG::ModKey> mod_keys)
{ DoubleClickedSignal(m_policy); }

void PolicyControl::RClick(const GG::Pt& pt, GG::Flags<GG::ModKey> mod_keys)
{ RightClickedSignal(m_policy, pt); }


//////////////////////////////////////////////////
// PolicysListBox                               //
//////////////////////////////////////////////////
/** Arrangement of PolicyControls that can be dragged onto SlotControls */
class PolicysListBox : public CUIListBox {
public:
    class PolicysListBoxRow : public CUIListBox::Row {
    public:
        PolicysListBoxRow(GG::X w, GG::Y h);
        void ChildrenDraggedAway(const std::vector<GG::Wnd*>& wnds,
                                 const GG::Wnd* destination) override;
    };

    /** \name Structors */ //@{
    PolicysListBox();
    //@}

    /** \name Accessors */ //@{
    const std::set<std::string>&    GetCategoriesShown() const;
    //@}

    /** \name Mutators */ //@{
    void SizeMove(const GG::Pt& ul, const GG::Pt& lr) override;
    void AcceptDrops(const GG::Pt& pt, std::vector<std::shared_ptr<GG::Wnd>> wnds,
                     GG::Flags<GG::ModKey> mod_keys) override;

    void            Populate();

    void            ShowCategory(const std::string& category, bool refresh_list = true);
    void            ShowAllCategories(bool refresh_list = true);
    void            HideCategory(const std::string& category, bool refresh_list = true);
    void            HideAllCategories(bool refresh_list = true);
    //@}

    mutable boost::signals2::signal<void (const Policy*, GG::Flags<GG::ModKey>)> PolicyTypeClickedSignal;
    mutable boost::signals2::signal<void (const Policy*)> PolicyTypeDoubleClickedSignal;
    mutable boost::signals2::signal<void (const Policy*, const GG::Pt& pt)> PolicyTypeRightClickedSignal;

protected:
    void DropsAcceptable(DropsAcceptableIter first, DropsAcceptableIter last,
                         const GG::Pt& pt, GG::Flags<GG::ModKey> mod_keys) const override;

private:
    std::set<std::string>   m_policy_categories_shown;  // which policy categories should be shown
    int                     m_previous_num_columns;
};

PolicysListBox::PolicysListBoxRow::PolicysListBoxRow(GG::X w, GG::Y h, const AvailabilityManager& availabilities_state) :
    CUIListBox::Row(w, h, "")   // drag_drop_data_type = "" implies not draggable row
{}

void PolicysListBox::PolicysListBoxRow::ChildrenDraggedAway(
    const std::vector<GG::Wnd*>& wnds, const GG::Wnd* destination)
{
    if (wnds.empty())
        return;
    // should only be one wnd in list because PolicyControls doesn't allow selection, so dragging is
    // only one-at-a-time
    auto control = dynamic_cast<GG::Control*>(wnds.front());
    if (!control || empty())
        return;

    // find control in row
    unsigned int ii = 0;
    for (; ii < size(); ++ii) {
        if (at(ii) != control)
            continue;
    }

    if (ii == size())
        return;

    RemoveCell(ii);  // Wnd that accepts drop takes ownership of dragged-away control

    auto policy_control = dynamic_cast<PolicyControl*>(control);
    if (!policy_control)
        return;

    const auto policy_type = policy_control->Policy();
    if (!policy_type)
        return;

    auto new_policy_control = GG::Wnd::Create<PolicyControl>(policy_type);
    const auto parent = dynamic_cast<const PolicysListBox*>(Parent().get());
    if (parent) {
        new_policy_control->ClickedSignal.connect(parent->PolicyTypeClickedSignal);
        new_policy_control->DoubleClickedSignal.connect(parent->PolicyTypeDoubleClickedSignal);
        new_policy_control->RightClickedSignal.connect(parent->PolicyTypeRightClickedSignal);
    }

    // set availability shown
    auto shown = m_availabilities_state.DisplayedPolicyAvailability(policy_type->Name());
    if (shown)
        new_policy_control->SetAvailability(*shown);

    SetCell(ii, new_policy_control);
}

PolicysListBox::PolicysListBox(const AvailabilityManager& availabilities_state) :
    CUIListBox(),
    m_policy_categories_shown(),
    m_previous_num_columns(-1)
{
    ManuallyManageColProps();
    NormalizeRowsOnInsert(false);
    SetStyle(GG::LIST_NOSEL);
}

const std::set<ShipPolicyClass>& PolicysListBox::GetCategoriesShown() const
{ return m_policy_categories_shown; }

void PolicysListBox::SizeMove(const GG::Pt& ul, const GG::Pt& lr) {
    GG::Pt old_size = GG::Wnd::Size();

    // maybe later do something interesting with docking
    CUIListBox::SizeMove(ul, lr);

    if (old_size != GG::Wnd::Size()) {
        // determine how many columns can fit in the box now...
        const GG::X TOTAL_WIDTH = Size().x - ClientUI::ScrollWidth();
        const int NUM_COLUMNS = std::max(1,
            Value(TOTAL_WIDTH / (SLOT_CONTROL_WIDTH + GG::X(PAD))));

        if (NUM_COLUMNS != m_previous_num_columns)
            Populate();
    }
}

/** Accept policys being discarded from the ship under design.*/
void PolicysListBox::AcceptDrops(const GG::Pt& pt,
                                 std::vector<std::shared_ptr<GG::Wnd>> wnds,
                                 GG::Flags<GG::ModKey> mod_keys)
{
    // If ctrl is pressed then signal all policys of the same type to be cleared.
    if (!(GG::GUI::GetGUI()->ModKeys() & GG::MOD_KEY_CTRL))
        return;

    if (wnds.empty())
        return;

    const PolicyControl* control = boost::polymorphic_downcast<const PolicyControl*>(wnds.begin()->get());
    const Policy* policy_type = control ? control->Policy() : nullptr;
    if (!policy_type)
        return;

    ClearPolicySignal(policy_type->Name());
}

PolicyGroupsType PolicysListBox::GroupAvailableDisplayablePolicys(const Empire* empire) {
    PolicyGroupsType policy_groups;
    // loop through all possible policys
    for (const auto& entry : GetPolicyTypeManager()) {
        const auto& policy = entry.second;
        if (!policy->Producible())
            continue;

        // check whether this policy should be shown in list
        ShipPolicyClass policy_class = policy->Class();
        if (m_policy_classes_shown.find(policy_class) == m_policy_classes_shown.end())
            continue;   // policy of this class is not requested to be shown

        // Check if policy satisfies availability and obsolecense
        auto shown = m_availabilities_state.DisplayedPolicyAvailability(policy->Name());
        if (!shown)
            continue;

        for (ShipSlotType slot_type : policy->MountableSlotTypes()) {
            policy_groups[{policy_class, slot_type}].push_back(policy.get());
        }
    }
    return policy_groups;
}

// Checks if the Location condition of the check_policy totally contains the Location condition of ref_policy
// i,e,, the ref_policy condition is met anywhere the check_policy condition is
bool LocationASubsumesLocationB(const Condition::ConditionBase* check_policy_loc,
                                const Condition::ConditionBase* ref_policy_loc)
{
    //const Condition::ConditionBase* check_policy_loc = check_policy->Location();
    //const Condition::ConditionBase* ref_policy_loc = ref_policy->Location();
    if (dynamic_cast<const Condition::All*>(ref_policy_loc))
        return true;
    if (!check_policy_loc || !ref_policy_loc)
        return false;
    if (*check_policy_loc == *ref_policy_loc)
        return true;
    // could do more involved checking for And conditions & Or, etc,
    // for now, will simply be conservative
    return false;
}

bool PolicyALocationSubsumesPolicyB(const Policy* check_policy, const Policy* ref_policy) {
    static std::map<std::pair<std::string, std::string>, bool> policy_loc_comparison_map;

    auto policy_pair = std::make_pair(check_policy->Name(), ref_policy->Name());
    auto map_it = policy_loc_comparison_map.find(policy_pair);
    if (map_it != policy_loc_comparison_map.end())
        return map_it->second;

    bool result = true;
    if (check_policy->Name() == "SH_MULTISPEC" || ref_policy->Name() == "SH_MULTISPEC")
        result = false;

    auto check_policy_loc = check_policy->Location();
    auto ref_policy_loc = ref_policy->Location();
    result = result && LocationASubsumesLocationB(check_policy_loc, ref_policy_loc);
    policy_loc_comparison_map[policy_pair] = result;
    //if (result && check_policy_loc && ref_policy_loc) {
    //    DebugLogger() << "Location for policyA, " << check_policy->Name() << ", subsumes that for policyB, " << ref_policy->Name();
    //    DebugLogger() << "   ...PolicyA Location is " << check_policy_loc->Description();
    //    DebugLogger() << "   ...PolicyB Location is " << ref_policy_loc->Description();
    //}
    return result;
}

void PolicysListBox::CullSuperfluousPolicys(std::vector<const Policy* >& this_group,
                                        ShipPolicyClass pclass, int empire_id, int loc_id)
{
    /// This is not merely a check for obsolescence; see PolicysListBox::Populate for more info
    static float min_bargain_ratio = -1.0;
    static float max_cost_ratio = -1.0;
    static float max_time_ratio = -1.0;

    if (min_bargain_ratio == -1.0) {
        min_bargain_ratio = 1.0;
        try {
            if (UserStringExists("FUNCTIONAL_MIN_BARGAIN_RATIO")) {
                float new_bargain_ratio = std::atof(UserString("FUNCTIONAL_MIN_BARGAIN_RATIO").c_str());
                if (new_bargain_ratio > 1.0f)
                    min_bargain_ratio = new_bargain_ratio;
            }
        } catch (...) {}
    }

    if (max_cost_ratio == -1.0) {
        max_cost_ratio = 1.0;
        try {
            if (UserStringExists("FUNCTIONAL_MAX_COST_RATIO")) {
                float new_cost_ratio = std::atof(UserString("FUNCTIONAL_MAX_COST_RATIO").c_str());
                if (new_cost_ratio > 1.0f)
                    max_cost_ratio = new_cost_ratio;
            }
        } catch (...) {}
    }

    if (max_time_ratio == -1.0) {
        max_time_ratio = 1.0;
        try {
            if (UserStringExists("FUNCTIONAL_MAX_TIME_RATIO")) {
                float new_time_ratio = std::atof(UserString("FUNCTIONAL_MAX_TIME_RATIO").c_str());
                if (new_time_ratio > 1.0f)
                    max_time_ratio = new_time_ratio;
            }
        } catch (...) {}
    }

    for (auto policy_it = this_group.begin();
         policy_it != this_group.end(); ++policy_it)
    {
        const Policy* checkPolicy = *policy_it;
        for (const Policy* ref_policy : this_group) {
            float cap_check = GetMainStat(checkPolicy);
            float cap_ref = GetMainStat(ref_policy);
            if ((cap_check < 0.0f) || (cap_ref < 0.0f))
                continue;  // not intended to handle such cases
            float cap_ratio = cap_ref / std::max(cap_check, 1e-4f) ;  // some policy types currently have zero capacity, but need to reject if both are zero
            float cost_check = checkPolicy->ProductionCost(empire_id, loc_id);
            float cost_ref = ref_policy->ProductionCost(empire_id, loc_id);
            if ((cost_check < 0.0f) || (cost_ref < 0.0f))
                continue;  // not intended to handle such cases
            float cost_ratio = (cost_ref + 1e-4) / (cost_check + 1e-4);  // can accept if somehow they both have cost zero
            float bargain_ratio = cap_ratio / std::max(cost_ratio, 1e-4f);
            float time_ratio = float(std::max(1, ref_policy->ProductionTime(empire_id, loc_id))) / std::max(1, checkPolicy->ProductionTime(empire_id, loc_id));
            // adjusting the max cost ratio to 1.4 or higher, will allow, for example, for
            // Zortium armor to make Standard armor redundant.  Setting a min_bargain_ratio higher than one can keep
            // trivial bargains from blocking lower valued policys.
            // TODO: move these values into default/customizations/common_user_customizations.txt  once that is supported

            if ((cap_ratio > 1.0) && ((cost_ratio <= 1.0) || ((bargain_ratio >= min_bargain_ratio) && (cost_ratio <= max_cost_ratio))) &&
                (time_ratio <= max_time_ratio) && PolicyALocationSubsumesPolicyB(checkPolicy, ref_policy))
            {
                //DebugLogger() << "Filtering " << checkPolicy->Name() << " because of " << ref_policy->Name();
                this_group.erase(policy_it--);
                break;
            }
        }

    }
}

void PolicysListBox::Populate() {
    ScopedTimer scoped_timer("PolicysListBox::Populate");

    const GG::X TOTAL_WIDTH = ClientWidth() - ClientUI::ScrollWidth();
    const int NUM_COLUMNS = std::max(1, Value(TOTAL_WIDTH / (SLOT_CONTROL_WIDTH + GG::X(PAD))));

    int empire_id = HumanClientApp::GetApp()->EmpireID();
    const Empire* empire = GetEmpire(empire_id);  // may be nullptr

    int cur_col = NUM_COLUMNS;
    std::shared_ptr<PolicysListBoxRow> cur_row;
    int num_policys = 0;

    // remove policys currently in rows of listbox
    Clear();

    /**
     * The Policys are first filtered for availability to this empire and according to the current
     * selections of which policy classes are to be displayed.  Then, in order to eliminate presentation
     * of clearly suboptimal policys, such as Mass Driver I when Mass Driver II is available at the same
     * cost & build time, some orgnization, paring and sorting of policys is done. The previously
     * filtered policys are grouped according to (class, slot).  Within each group, policys are compared
     * and pared for display; only policys within the same group may suppress display of each other.
     * The paring is (currently) done on the basis of main stat, construction cost, and construction
     * time. If two policys have the same class and slot, and one has a lower main stat but also a lower
     * cost, they will both be presented; if one has a higher main stat and is at least as good on cost
     * and time, it will suppress the other.
     *
     * An example of one of the more subtle possible results is that if a policy class had multiple policys
     * with different but overlapping MountableSlotType patterns, then a policy with two possible slot
     * types might be rendered superfluous for the first slot type by a first other policy, be rendered
     * superfluous for the second slot type by a second other policy, even if neither of the latter two
     * policys would be considered to individually render the former policy obsolete.
     */

    /// filter policys by availability and current designation of classes for display; group according to (class, slot)
    PolicyGroupsType policy_groups = GroupAvailableDisplayablePolicys(empire);

    // get empire id and location to use for cost and time comparisons
    int loc_id = INVALID_OBJECT_ID;
    if (empire) {
        auto location = GetUniverseObject(empire->CapitalID());
        loc_id = location ? location->ID() : INVALID_OBJECT_ID;
    }

    // if showing policys for a policyicular empire, cull redundant policys (if enabled)
    if (empire) {
        for (auto& policy_group : policy_groups) {
            ShipPolicyClass pclass = policy_group.first.first;
            if (!m_show_superfluous_policys)
                CullSuperfluousPolicys(policy_group.second, pclass, empire_id, loc_id);
        }
    }

    // now sort the policys within each group according to main stat, via weak
    // sorting in a multimap also, if a policy was in multiple groups due to being
    // compatible with multiple slot types, ensure it is only displayed once
    std::set<const Policy* > already_added;
    for (auto& policy_group : policy_groups) {
        std::multimap<double, const Policy*> sorted_group;
        for (const Policy* policy : policy_group.second) {
            if (already_added.find(policy) != already_added.end())
                continue;
            already_added.insert(policy);
            sorted_group.insert({GetMainStat(policy), policy});
        }

        // take the sorted policys and make UI elements (technically rows) for the PolicysListBox
        for (auto& group : sorted_group) {
            const Policy* policy = group.second;
            // check if current row is full, and make a new row if necessary
            if (cur_col >= NUM_COLUMNS) {
                if (cur_row)
                    Insert(cur_row);
                cur_col = 0;
                cur_row = GG::Wnd::Create<PolicysListBoxRow>(
                    TOTAL_WIDTH, SLOT_CONTROL_HEIGHT + GG::Y(PAD), m_availabilities_state);
            }
            ++cur_col;
            ++num_policys;

            // make new policy control and add to row
            auto control = GG::Wnd::Create<PolicyControl>(policy);
            control->ClickedSignal.connect(
                PolicysListBox::PolicyTypeClickedSignal);
            control->DoubleClickedSignal.connect(
                PolicysListBox::PolicyTypeDoubleClickedSignal);
            control->RightClickedSignal.connect(
                PolicysListBox::PolicyTypeRightClickedSignal);

            auto shown = m_availabilities_state.DisplayedPolicyAvailability(policy->Name());
            if (shown)
                control->SetAvailability(*shown);

            cur_row->push_back(control);
        }
    }
    // add any incomplete rows
    if (cur_row)
        Insert(cur_row);

    // keep track of how many columns are present now
    m_previous_num_columns = NUM_COLUMNS;

    // If there are no policys add a prompt to suggest a solution.
    if (num_policys == 0)
        Insert(GG::Wnd::Create<PromptRow>(TOTAL_WIDTH,
                                          UserString("ALL_AVAILABILITY_FILTERS_BLOCKING_PROMPT")),
               begin(), false);

}

void PolicysListBox::ShowClass(ShipPolicyClass policy_class, bool refresh_list) {
    if (m_policy_classes_shown.find(policy_class) == m_policy_classes_shown.end()) {
        m_policy_classes_shown.insert(policy_class);
        if (refresh_list)
            Populate();
    }
}

void PolicysListBox::ShowAllClasses(bool refresh_list) {
    for (ShipPolicyClass policy_class = ShipPolicyClass(0); policy_class != NUM_SHIP_PART_CLASSES; policy_class = ShipPolicyClass(policy_class + 1))
        m_policy_classes_shown.insert(policy_class);
    if (refresh_list)
        Populate();
}

void PolicysListBox::HideClass(ShipPolicyClass policy_class, bool refresh_list) {
    auto it = m_policy_classes_shown.find(policy_class);
    if (it != m_policy_classes_shown.end()) {
        m_policy_classes_shown.erase(it);
        if (refresh_list)
            Populate();
    }
}

void PolicysListBox::HideAllClasses(bool refresh_list) {
    m_policy_classes_shown.clear();
    if (refresh_list)
        Populate();
}

void PolicysListBox::ShowSuperfluousPolicys(bool refresh_list) {
    if (m_show_superfluous_policys)
        return;
    m_show_superfluous_policys = true;
    if (refresh_list)
        Populate();
}

void PolicysListBox::HideSuperfluousPolicys(bool refresh_list) {
    if (!m_show_superfluous_policys)
        return;
    m_show_superfluous_policys = false;
    if (refresh_list)
        Populate();
}


//////////////////////////////////////////////////
// DesignWnd::PolicyPalette                       //
//////////////////////////////////////////////////
/** Contains graphical list of PolicyControl which can be dragged and dropped
  * onto slots to assign policys to those slots */
class DesignWnd::PolicyPalette : public CUIWnd {
public:
    /** \name Structors */ //@{
    PolicyPalette(const std::string& config_name);
    void CompleteConstruction() override;
    //@}

    /** \name Mutators */ //@{
    void SizeMove(const GG::Pt& ul, const GG::Pt& lr) override;

    void ShowClass(ShipPolicyClass policy_class, bool refresh_list = true);
    void ShowAllClasses(bool refresh_list = true);
    void HideClass(ShipPolicyClass policy_class, bool refresh_list = true);
    void HideAllClasses(bool refresh_list = true);
    void ToggleClass(ShipPolicyClass policy_class, bool refresh_list = true);
    void ToggleAllClasses(bool refresh_list = true);

    void ToggleAvailability(const Availability::Enum type);

    void ShowSuperfluous(bool refresh_list = true);
    void HideSuperfluous(bool refresh_list = true);
    void ToggleSuperfluous(bool refresh_list = true);

    void Populate();
    //@}

    mutable boost::signals2::signal<void (const Policy*, GG::Flags<GG::ModKey>)> PolicyTypeClickedSignal;
    mutable boost::signals2::signal<void (const Policy*)> PolicyTypeDoubleClickedSignal;
    mutable boost::signals2::signal<void (const Policy*, const GG::Pt& pt)> PolicyTypeRightClickedSignal;
    mutable boost::signals2::signal<void ()> PolicyObsolescenceChangedSignal;
    mutable boost::signals2::signal<void (const std::string&)> ClearPolicySignal;

private:
    void DoLayout();

    /** A policy type click with ctrl obsoletes policy. */
    void HandlePolicyTypeClicked(const Policy*, GG::Flags<GG::ModKey>);
    void HandlePolicyTypeRightClicked(const Policy*, const GG::Pt& pt);

    std::shared_ptr<PolicysListBox>   m_policys_list;

    std::map<ShipPolicyClass, std::shared_ptr<CUIStateButton>>    m_class_buttons;
    std::shared_ptr<CUIStateButton>                             m_superfluous_policys_button;

    // Holds the state of the availabilities filter.
    AvailabilityManager m_availabilities_state;
    std::tuple<std::shared_ptr<CUIStateButton>,
               std::shared_ptr<CUIStateButton>,
               std::shared_ptr<CUIStateButton>> m_availabilities_buttons;

};

DesignWnd::PolicyPalette::PolicyPalette(const std::string& config_name) :
    CUIWnd(UserString("DESIGN_WND_PART_PALETTE_TITLE"),
           GG::ONTOP | GG::INTERACTIVE | GG::DRAGABLE | GG::RESIZABLE,
           config_name),
    m_policys_list(nullptr),
    m_superfluous_policys_button(nullptr),
    m_availabilities_state(false, true, false)
{}

void DesignWnd::PolicyPalette::CompleteConstruction() {
    //TempUISoundDisabler sound_disabler;     // should be redundant with disabler in DesignWnd::DesignWnd.  uncomment if this is not the case
    SetChildClippingMode(ClipToClient);

    m_policys_list = GG::Wnd::Create<PolicysListBox>(m_availabilities_state);
    AttachChild(m_policys_list);
    m_policys_list->PolicyTypeClickedSignal.connect(
        boost::bind(&DesignWnd::PolicyPalette::HandlePolicyTypeClicked, this, _1, _2));
    m_policys_list->PolicyTypeDoubleClickedSignal.connect(
        PolicyTypeDoubleClickedSignal);
    m_policys_list->PolicyTypeRightClickedSignal.connect(
        boost::bind(&DesignWnd::PolicyPalette::HandlePolicyTypeRightClicked, this, _1, _2));
    m_policys_list->ClearPolicySignal.connect(ClearPolicySignal);

    const PolicyTypeManager& policy_manager = GetPolicyTypeManager();

    // class buttons
    for (ShipPolicyClass policy_class = ShipPolicyClass(0); policy_class != NUM_SHIP_PART_CLASSES; policy_class = ShipPolicyClass(policy_class + 1)) {
        // are there any policys of this class?
        bool policy_of_this_class_exists = false;
        for (const auto& entry : policy_manager) {
            if (const auto& policy = entry.second) {
                if (policy->Class() == policy_class) {
                    policy_of_this_class_exists = true;
                    break;
                }
            }
        }
        if (!policy_of_this_class_exists)
            continue;

        m_class_buttons[policy_class] = GG::Wnd::Create<CUIStateButton>(UserString(boost::lexical_cast<std::string>(policy_class)), GG::FORMAT_CENTER, std::make_shared<CUILabelButtonRepresenter>());
        AttachChild(m_class_buttons[policy_class]);
        m_class_buttons[policy_class]->CheckedSignal.connect(
            boost::bind(&DesignWnd::PolicyPalette::ToggleClass, this, policy_class, true));
    }

    // availability buttons
    // TODO: C++17, Collect and replace with structured binding auto [a, b, c] = m_availabilities;
    auto& m_obsolete_button = std::get<Availability::Obsolete>(m_availabilities_buttons);
    m_obsolete_button = GG::Wnd::Create<CUIStateButton>(UserString("PRODUCTION_WND_AVAILABILITY_OBSOLETE"), GG::FORMAT_CENTER, std::make_shared<CUILabelButtonRepresenter>());
    AttachChild(m_obsolete_button);
    m_obsolete_button->CheckedSignal.connect(
        boost::bind(&DesignWnd::PolicyPalette::ToggleAvailability, this, Availability::Obsolete));
    m_obsolete_button->SetCheck(m_availabilities_state.GetAvailability(Availability::Obsolete));

    auto& m_available_button = std::get<Availability::Available>(m_availabilities_buttons);
    m_available_button = GG::Wnd::Create<CUIStateButton>(UserString("PRODUCTION_WND_AVAILABILITY_AVAILABLE"), GG::FORMAT_CENTER, std::make_shared<CUILabelButtonRepresenter>());
    AttachChild(m_available_button);
    m_available_button->CheckedSignal.connect(
        boost::bind(&DesignWnd::PolicyPalette::ToggleAvailability, this, Availability::Available));
    m_available_button->SetCheck(m_availabilities_state.GetAvailability(Availability::Available));

    auto& m_unavailable_button = std::get<Availability::Future>(m_availabilities_buttons);
    m_unavailable_button = GG::Wnd::Create<CUIStateButton>(UserString("PRODUCTION_WND_AVAILABILITY_UNAVAILABLE"), GG::FORMAT_CENTER, std::make_shared<CUILabelButtonRepresenter>());
    AttachChild(m_unavailable_button);
    m_unavailable_button->CheckedSignal.connect(
        boost::bind(&DesignWnd::PolicyPalette::ToggleAvailability, this, Availability::Future));
    m_unavailable_button->SetCheck(m_availabilities_state.GetAvailability(Availability::Future));

    // superfluous policys button
    m_superfluous_policys_button = GG::Wnd::Create<CUIStateButton>(UserString("PRODUCTION_WND_REDUNDANT"), GG::FORMAT_CENTER, std::make_shared<CUILabelButtonRepresenter>());
    AttachChild(m_superfluous_policys_button);
    m_superfluous_policys_button->CheckedSignal.connect(
        boost::bind(&DesignWnd::PolicyPalette::ToggleSuperfluous, this, true));

    // default to showing nothing
    ShowAllClasses(false);
    ShowSuperfluous(false);
    Populate();

    CUIWnd::CompleteConstruction();

    DoLayout();
    SaveDefaultedOptions();
}

void DesignWnd::PolicyPalette::SizeMove(const GG::Pt& ul, const GG::Pt& lr) {
    CUIWnd::SizeMove(ul, lr);
    DoLayout();
}

void DesignWnd::PolicyPalette::DoLayout() {
    const int PTS = ClientUI::Pts();
    const GG::X PTS_WIDE(PTS/2);         // guess at how wide per character the font needs
    const GG::Y  BUTTON_HEIGHT(PTS*3/2);
    const int BUTTON_SEPARATION = 3;    // vertical or horizontal sepration between adjacent buttons
    const int BUTTON_EDGE_PAD = 2;      // distance from edges of control to buttons
    const GG::X RIGHT_EDGE_PAD(8);       // to account for border of CUIWnd

    const GG::X USABLE_WIDTH = std::max(ClientWidth() - RIGHT_EDGE_PAD, GG::X1);   // space in which to fit buttons
    const int GUESSTIMATE_NUM_CHARS_IN_BUTTON_LABEL = 14;                   // rough guesstimate... avoid overly long policy class names
    const GG::X MIN_BUTTON_WIDTH = PTS_WIDE*GUESSTIMATE_NUM_CHARS_IN_BUTTON_LABEL;
    const int MAX_BUTTONS_PER_ROW = std::max(Value(USABLE_WIDTH / (MIN_BUTTON_WIDTH + BUTTON_SEPARATION)), 1);

    const int NUM_CLASS_BUTTONS = std::max(1, static_cast<int>(m_class_buttons.size()));
    const int NUM_SUPERFLUOUS_CULL_BUTTONS = 1;
    const int NUM_AVAILABILITY_BUTTONS = 3;
    const int NUM_NON_CLASS_BUTTONS = NUM_SUPERFLUOUS_CULL_BUTTONS + NUM_AVAILABILITY_BUTTONS;

    // determine whether to put non-class buttons (availability and redundancy)
    // in one column or two.
    // -> if class buttons fill up fewer rows than (the non-class buttons in one
    // column), split the non-class buttons into two columns
    int num_non_class_buttons_per_row = 1;
    if (NUM_CLASS_BUTTONS < NUM_NON_CLASS_BUTTONS*(MAX_BUTTONS_PER_ROW - num_non_class_buttons_per_row))
        num_non_class_buttons_per_row = 2;

    const int MAX_CLASS_BUTTONS_PER_ROW = std::max(1, MAX_BUTTONS_PER_ROW - num_non_class_buttons_per_row);

    const int NUM_CLASS_BUTTON_ROWS = static_cast<int>(std::ceil(static_cast<float>(NUM_CLASS_BUTTONS) / MAX_CLASS_BUTTONS_PER_ROW));
    const int NUM_CLASS_BUTTONS_PER_ROW = static_cast<int>(std::ceil(static_cast<float>(NUM_CLASS_BUTTONS) / NUM_CLASS_BUTTON_ROWS));

    const int TOTAL_BUTTONS_PER_ROW = NUM_CLASS_BUTTONS_PER_ROW + num_non_class_buttons_per_row;

    const GG::X BUTTON_WIDTH = (USABLE_WIDTH - (TOTAL_BUTTONS_PER_ROW - 1)*BUTTON_SEPARATION) / TOTAL_BUTTONS_PER_ROW;

    const GG::X COL_OFFSET = BUTTON_WIDTH + BUTTON_SEPARATION;    // horizontal distance between each column of buttons
    const GG::Y ROW_OFFSET = BUTTON_HEIGHT + BUTTON_SEPARATION;   // vertical distance between each row of buttons

    // place class buttons
    int col = NUM_CLASS_BUTTONS_PER_ROW;
    int row = -1;
    for (auto& entry : m_class_buttons) {
        if (col >= NUM_CLASS_BUTTONS_PER_ROW) {
            col = 0;
            ++row;
        }
        GG::Pt ul(BUTTON_EDGE_PAD + col*COL_OFFSET, BUTTON_EDGE_PAD + row*ROW_OFFSET);
        GG::Pt lr = ul + GG::Pt(BUTTON_WIDTH, BUTTON_HEIGHT);
        entry.second->SizeMove(ul, lr);
        ++col;
    }

    // place policys list.  note: assuming at least as many rows of class buttons as availability buttons, as should
    //                          be the case given how num_non_class_buttons_per_row is determined
    m_policys_list->SizeMove(GG::Pt(GG::X0, BUTTON_EDGE_PAD + ROW_OFFSET*(row + 1)),
                           ClientSize() - GG::Pt(GG::X(BUTTON_SEPARATION), GG::Y(BUTTON_SEPARATION)));

    // place slot type buttons
    col = NUM_CLASS_BUTTONS_PER_ROW;
    row = 0;
    auto ul = GG::Pt(BUTTON_EDGE_PAD + col*COL_OFFSET, BUTTON_EDGE_PAD + row*ROW_OFFSET);
    auto lr = ul + GG::Pt(BUTTON_WIDTH, BUTTON_HEIGHT);
    m_superfluous_policys_button->SizeMove(ul, lr);

    // a function to place availability buttons either in a single column below the
    // superfluous button or to complete a 2X2 grid left of the class buttons.
    auto place_avail_button_adjacent =
        [&col, &row, &num_non_class_buttons_per_row, NUM_CLASS_BUTTONS_PER_ROW,
         BUTTON_EDGE_PAD, COL_OFFSET, ROW_OFFSET, BUTTON_WIDTH, BUTTON_HEIGHT]
        (GG::Wnd* avail_btn)
        {
            if (num_non_class_buttons_per_row == 1)
                ++row;
            else {
                if (col >= NUM_CLASS_BUTTONS_PER_ROW + num_non_class_buttons_per_row - 1) {
                    col = NUM_CLASS_BUTTONS_PER_ROW - 1;
                    ++row;
                }
                ++col;
            }

            auto ul = GG::Pt(BUTTON_EDGE_PAD + col*COL_OFFSET, BUTTON_EDGE_PAD + row*ROW_OFFSET);
            auto lr = ul + GG::Pt(BUTTON_WIDTH, BUTTON_HEIGHT);
            avail_btn->SizeMove(ul, lr);
        };

    //place availability buttons
    // TODO: C++17, Replace with structured binding auto [a, b, c] = m_availabilities;
    auto& m_obsolete_button = std::get<Availability::Obsolete>(m_availabilities_buttons);
    auto& m_available_button = std::get<Availability::Available>(m_availabilities_buttons);
    auto& m_unavailable_button = std::get<Availability::Future>(m_availabilities_buttons);

    place_avail_button_adjacent(m_obsolete_button.get());
    place_avail_button_adjacent(m_available_button.get());
    place_avail_button_adjacent(m_unavailable_button.get());
}

void DesignWnd::PolicyPalette::HandlePolicyTypeClicked(const Policy* policy_type, GG::Flags<GG::ModKey> modkeys) {
    // Toggle obsolete for a control click.
    if (modkeys & GG::MOD_KEY_CTRL) {
        auto& manager = GetCurrentDesignsManager();
        const auto obsolete = manager.IsPolicyObsolete(policy_type->Name());
        manager.SetPolicyObsolete(policy_type->Name(), !obsolete);

        PolicyObsolescenceChangedSignal();
        Populate();
    }
    else
        PolicyTypeClickedSignal(policy_type, modkeys);
}

void DesignWnd::PolicyPalette::HandlePolicyTypeRightClicked(const Policy* policy_type, const GG::Pt& pt) {
    // Context menu actions
    auto& manager = GetCurrentDesignsManager();
    const auto& policy_name = policy_type->Name();
    auto is_obsolete = manager.IsPolicyObsolete(policy_name);
    auto toggle_obsolete_design_action = [&manager, &policy_name, is_obsolete, this]() {
        manager.SetPolicyObsolete(policy_name, !is_obsolete);
        PolicyObsolescenceChangedSignal();
        Populate();
    };

    // create popup menu with a commands in it
    auto popup = GG::Wnd::Create<CUIPopupMenu>(pt.x, pt.y);

    const auto empire_id = HumanClientApp::GetApp()->EmpireID();
    if (empire_id != ALL_EMPIRES)
        popup->AddMenuItem(GG::MenuItem(
                               (is_obsolete
                                ? UserString("DESIGN_WND_UNOBSOLETE_PART")
                                : UserString("DESIGN_WND_OBSOLETE_PART")),
                               false, false, toggle_obsolete_design_action));

    popup->Run();

    PolicyTypeRightClickedSignal(policy_type, pt);
}

void DesignWnd::PolicyPalette::ShowClass(ShipPolicyClass policy_class, bool refresh_list) {
    if (policy_class >= ShipPolicyClass(0) && policy_class < NUM_SHIP_PART_CLASSES) {
        m_policys_list->ShowClass(policy_class, refresh_list);
        m_class_buttons[policy_class]->SetCheck();
    } else {
        throw std::invalid_argument("PolicyPalette::ShowClass was passed an invalid ShipPolicyClass");
    }
}

void DesignWnd::PolicyPalette::ShowAllClasses(bool refresh_list) {
    m_policys_list->ShowAllClasses(refresh_list);
    for (auto& entry : m_class_buttons)
        entry.second->SetCheck();
}

void DesignWnd::PolicyPalette::HideClass(ShipPolicyClass policy_class, bool refresh_list) {
    if (policy_class >= ShipPolicyClass(0) && policy_class < NUM_SHIP_PART_CLASSES) {
        m_policys_list->HideClass(policy_class, refresh_list);
        m_class_buttons[policy_class]->SetCheck(false);
    } else {
        throw std::invalid_argument("PolicyPalette::HideClass was passed an invalid ShipPolicyClass");
    }
}

void DesignWnd::PolicyPalette::HideAllClasses(bool refresh_list) {
    m_policys_list->HideAllClasses(refresh_list);
    for (auto& entry : m_class_buttons)
        entry.second->SetCheck(false);
}

void DesignWnd::PolicyPalette::ToggleClass(ShipPolicyClass policy_class, bool refresh_list) {
    if (policy_class >= ShipPolicyClass(0) && policy_class < NUM_SHIP_PART_CLASSES) {
        const auto& classes_shown = m_policys_list->GetClassesShown();
        if (classes_shown.find(policy_class) == classes_shown.end())
            ShowClass(policy_class, refresh_list);
        else
            HideClass(policy_class, refresh_list);
    } else {
        throw std::invalid_argument("PolicyPalette::ToggleClass was passed an invalid ShipPolicyClass");
    }
}

void DesignWnd::PolicyPalette::ToggleAllClasses(bool refresh_list)
{
    const auto& classes_shown = m_policys_list->GetClassesShown();
    if (classes_shown.size() == NUM_SHIP_PART_CLASSES)
        HideAllClasses(refresh_list);
    else
        ShowAllClasses(refresh_list);
}

void DesignWnd::PolicyPalette::ToggleAvailability(Availability::Enum type) {
    std::shared_ptr<CUIStateButton> button;
    bool state = false;
    switch (type) {
    case Availability::Obsolete:
        m_availabilities_state.ToggleAvailability(Availability::Obsolete);
        state = m_availabilities_state.GetAvailability(Availability::Obsolete);
        button = std::get<Availability::Obsolete>(m_availabilities_buttons);
        break;
    case Availability::Available:
        m_availabilities_state.ToggleAvailability(Availability::Available);
        state = m_availabilities_state.GetAvailability(Availability::Available);
        button = std::get<Availability::Available>(m_availabilities_buttons);
        break;
    case Availability::Future:
        m_availabilities_state.ToggleAvailability(Availability::Future);
        state = m_availabilities_state.GetAvailability(Availability::Future);
        button = std::get<Availability::Future>(m_availabilities_buttons);
        break;
    }

    button->SetCheck(state);

    Populate();
}

void DesignWnd::PolicyPalette::ShowSuperfluous(bool refresh_list) {
    m_policys_list->ShowSuperfluousPolicys(refresh_list);
    m_superfluous_policys_button->SetCheck();
}

void DesignWnd::PolicyPalette::HideSuperfluous(bool refresh_list) {
    m_policys_list->HideSuperfluousPolicys(refresh_list);
    m_superfluous_policys_button->SetCheck(false);
}

void DesignWnd::PolicyPalette::ToggleSuperfluous(bool refresh_list) {
    bool showing_superfluous = m_policys_list->GetShowingSuperfluous();
    if (showing_superfluous)
        HideSuperfluous(refresh_list);
    else
        ShowSuperfluous(refresh_list);
}

void DesignWnd::PolicyPalette::Populate()
{ m_policys_list->Populate(); }


//////////////////////////////////////////////////
// BasesListBox                                  //
//////////////////////////////////////////////////
/** List of starting points for designs, such as empty hulls, existing designs
  * kept by this empire or seen elsewhere in the universe, design template
  * scripts or saved (on disk) designs from previous games. */
class BasesListBox : public QueueListBox {
public:
    static const std::string BASES_LIST_BOX_DROP_TYPE;

    /** \name Structors */ //@{
    BasesListBox(const AvailabilityManager& availabilities_state,
                 const boost::optional<std::string>& drop_type = boost::none,
                 const boost::optional<std::string>& empty_prompt = boost::none);
    //@}
    void CompleteConstruction() override;

    /** \name Accessors */ //@{
    //@}

    /** \name Mutators */ //@{
    void SizeMove(const GG::Pt& ul, const GG::Pt& lr) override;

    void ChildrenDraggedAway(const std::vector<GG::Wnd*>& wnds, const GG::Wnd* destination) override;

    virtual void QueueItemMoved(const GG::ListBox::iterator& row_it, const GG::ListBox::iterator& original_position_it)
    {}

    void SetEmpireShown(int empire_id, bool refresh_list = true);

    virtual void Populate();

    //@}

    mutable boost::signals2::signal<void (int)>                 DesignSelectedSignal;
    mutable boost::signals2::signal<void (const std::string&, const std::vector<std::string>&)>
                                                                DesignComponentsSelectedSignal;
    mutable boost::signals2::signal<void (const boost::uuids::uuid&)>  SavedDesignSelectedSignal;

    mutable boost::signals2::signal<void (const ShipDesign*)>   DesignClickedSignal;
    mutable boost::signals2::signal<void (const HullType*)>     HullClickedSignal;
    mutable boost::signals2::signal<void (const ShipDesign*)>   DesignRightClickedSignal;

    class HullAndNamePanel : public GG::Control {
    public:
        HullAndNamePanel(GG::X w, GG::Y h, const std::string& hull, const std::string& name);

        void CompleteConstruction() override;
        void SizeMove(const GG::Pt& ul, const GG::Pt& lr) override;

        void Render() override
        {}

        void SetAvailability(const AvailabilityManager::DisplayedAvailabilies& type);
        void SetDisplayName(const std::string& name);

    private:
        std::shared_ptr<GG::StaticGraphic>              m_graphic;
        std::shared_ptr<GG::Label>                      m_name;
    };

    class BasesListBoxRow : public CUIListBox::Row {
        public:
        BasesListBoxRow(GG::X w, GG::Y h, const std::string& hull, const std::string& name);

        void CompleteConstruction() override;
        void Render() override;

        void SizeMove(const GG::Pt& ul, const GG::Pt& lr) override;

        virtual void SetAvailability(const AvailabilityManager::DisplayedAvailabilies& type);
        virtual void SetDisplayName(const std::string& name);

        private:
        std::shared_ptr<HullAndNamePanel>               m_hull_panel;
    };

    class HullAndPolicysListBoxRow : public BasesListBoxRow {
    public:
        HullAndPolicysListBoxRow(GG::X w, GG::Y h, const std::string& hull,
                               const std::vector<std::string>& policys);
        void CompleteConstruction() override;
        const std::string&              Hull() const    { return m_hull_name; }
        const std::vector<std::string>& Policys() const   { return m_policys; }
    protected:
        std::string                     m_hull_name;
        std::vector<std::string>        m_policys;
    };

    class CompletedDesignListBoxRow : public BasesListBoxRow {
    public:
        CompletedDesignListBoxRow(GG::X w, GG::Y h, const ShipDesign& design);
        void CompleteConstruction() override;
        int                             DesignID() const { return m_design_id; }
    private:
        int                             m_design_id;
    };

protected:
    void ItemRightClickedImpl(GG::ListBox::iterator it, const GG::Pt& pt, const GG::Flags<GG::ModKey>& modkeys) override;

    /** An implementation of BasesListBox provides a PopulateCore to fill itself.*/
    virtual void PopulateCore() = 0;

    /** Reset the empty list prompt. */
    virtual void ResetEmptyListPrompt();

    /** If \p wnd is a valid dragged child return a replacement row.  Otherwise return nullptr. */
    virtual std::shared_ptr<Row> ChildrenDraggedAwayCore(const GG::Wnd* const wnd) = 0;

    /** \name Accessors for derived classes. */ //@{
    int EmpireID() const { return m_empire_id_shown; }

    const AvailabilityManager& AvailabilityState() const
    { return m_availabilities_state; }

    GG::Pt  ListRowSize();
    //@}

    virtual void  BaseDoubleClicked(GG::ListBox::iterator it, const GG::Pt& pt, const GG::Flags<GG::ModKey>& modkeys)
    {}
    virtual void  BaseLeftClicked(GG::ListBox::iterator it, const GG::Pt& pt, const GG::Flags<GG::ModKey>& modkeys)
    {}
    virtual void  BaseRightClicked(GG::ListBox::iterator it, const GG::Pt& pt, const GG::Flags<GG::ModKey>& modkeys)
    {}

private:

    void    InitRowSizes();

    int                         m_empire_id_shown;
    const AvailabilityManager& m_availabilities_state;

    boost::signals2::connection m_empire_designs_changed_signal;
};

BasesListBox::HullAndNamePanel::HullAndNamePanel(GG::X w, GG::Y h, const std::string& hull, const std::string& name) :
    GG::Control(GG::X0, GG::Y0, w, h, GG::NO_WND_FLAGS),
    m_graphic(nullptr),
    m_name(nullptr)
{
    SetChildClippingMode(ClipToClient);

    m_graphic = GG::Wnd::Create<GG::StaticGraphic>(ClientUI::HullIcon(hull),
                                                   GG::GRAPHIC_PROPSCALE | GG::GRAPHIC_FITGRAPHIC);
    m_graphic->Resize(GG::Pt(w, h));
    m_name = GG::Wnd::Create<CUILabel>(name, GG::FORMAT_WORDBREAK | GG::FORMAT_CENTER | GG::FORMAT_TOP);
}

void BasesListBox::HullAndNamePanel::CompleteConstruction() {
    GG::Control::CompleteConstruction();
    AttachChild(m_graphic);
    AttachChild(m_name);
}

void BasesListBox::HullAndNamePanel::SizeMove(const GG::Pt& ul, const GG::Pt& lr) {
    GG::Control::SizeMove(ul, lr);
    m_graphic->Resize(Size());
    m_name->Resize(Size());
}

void BasesListBox::HullAndNamePanel::SetAvailability(
    const AvailabilityManager::DisplayedAvailabilies& type)
{
    auto disabled = std::get<Availability::Obsolete>(type);
    m_graphic->Disable(disabled);
    m_name->Disable(disabled);
}

void BasesListBox::HullAndNamePanel::SetDisplayName(const std::string& name) {
    m_name->SetText(name);
    m_name->Resize(GG::Pt(Width(), m_name->Height()));
}

BasesListBox::BasesListBoxRow::BasesListBoxRow(GG::X w, GG::Y h, const std::string& hull, const std::string& name) :
    CUIListBox::Row(w, h, BASES_LIST_BOX_DROP_TYPE),
    m_hull_panel(nullptr)
{
    if (hull.empty()) {
        ErrorLogger() << "No hull name provided for ship row display.";
        return;
    }

    m_hull_panel = GG::Wnd::Create<HullAndNamePanel>(w, h, hull, name);

    SetBrowseModeTime(GetOptionsDB().Get<int>("ui.tooltip.delay"));
}

void BasesListBox::BasesListBoxRow::CompleteConstruction() {
    CUIListBox::Row::CompleteConstruction();
    push_back(m_hull_panel);
}

void BasesListBox::BasesListBoxRow::Render() {
    GG::Pt ul = UpperLeft();
    GG::Pt lr = LowerRight();
    GG::Pt ul_adjusted_for_drop_indicator = GG::Pt(ul.x, ul.y + GG::Y(1));
    GG::Pt lr_adjusted_for_drop_indicator = GG::Pt(lr.x, lr.y - GG::Y(2));
    GG::FlatRectangle(ul_adjusted_for_drop_indicator, lr_adjusted_for_drop_indicator,
                      ClientUI::WndColor(),
                      (Disabled() ? DisabledColor(GG::CLR_WHITE) : GG::CLR_WHITE), 1);
}

void BasesListBox::BasesListBoxRow::SizeMove(const GG::Pt& ul, const GG::Pt& lr) {
    const GG::Pt old_size = Size();
    CUIListBox::Row::SizeMove(ul, lr);
    if (!empty() && old_size != Size())
        at(0)->Resize(Size());
}

void BasesListBox::BasesListBoxRow::SetAvailability(const AvailabilityManager::DisplayedAvailabilies& type) {
    if (std::get<Availability::Obsolete>(type) && std::get<Availability::Future>(type))
        SetBrowseText(UserString("PRODUCTION_WND_AVAILABILITY_OBSOLETE_AND_UNAVAILABLE"));
    else if (std::get<Availability::Obsolete>(type))
        SetBrowseText(UserString("PRODUCTION_WND_AVAILABILITY_OBSOLETE"));
    else if (std::get<Availability::Future>(type))
        SetBrowseText(UserString("PRODUCTION_WND_AVAILABILITY_UNAVAILABLE"));
    else
        ClearBrowseInfoWnd();

    auto disabled = std::get<Availability::Obsolete>(type);
    Disable(disabled);
    if (m_hull_panel)
        m_hull_panel->SetAvailability(type);
}

void BasesListBox::BasesListBoxRow::SetDisplayName(const std::string& name) {
    if (m_hull_panel)
        m_hull_panel->SetDisplayName(name);
}

BasesListBox::HullAndPolicysListBoxRow::HullAndPolicysListBoxRow(GG::X w, GG::Y h, const std::string& hull,
                                                             const std::vector<std::string>& policys) :
    BasesListBoxRow(w, h, hull, UserString(hull)),
    m_hull_name(hull),
    m_policys(policys)
{}

void BasesListBox::HullAndPolicysListBoxRow::CompleteConstruction() {
    BasesListBoxRow::CompleteConstruction();
    SetDragDropDataType(HULL_PARTS_ROW_DROP_TYPE_STRING);
}

BasesListBox::CompletedDesignListBoxRow::CompletedDesignListBoxRow(
    GG::X w, GG::Y h, const ShipDesign &design) :
    BasesListBoxRow(w, h, design.Hull(), design.Name()),
    m_design_id(design.ID())
{}

void BasesListBox::CompletedDesignListBoxRow::CompleteConstruction() {
    BasesListBoxRow::CompleteConstruction();
    SetDragDropDataType(COMPLETE_DESIGN_ROW_DROP_STRING);
}

const std::string BasesListBox::BASES_LIST_BOX_DROP_TYPE = "BasesListBoxRow";

BasesListBox::BasesListBox(const AvailabilityManager& availabilities_state,
                           const boost::optional<std::string>& drop_type,
                           const boost::optional<std::string>& empty_prompt /*= boost::none*/) :
    QueueListBox(drop_type,
                 empty_prompt ? *empty_prompt : UserString("ADD_FIRST_DESIGN_DESIGN_QUEUE_PROMPT")),
    m_empire_id_shown(ALL_EMPIRES),
    m_availabilities_state(availabilities_state)
{}

void BasesListBox::CompleteConstruction() {
    QueueListBox::CompleteConstruction();

    InitRowSizes();
    SetStyle(GG::LIST_NOSEL | GG::LIST_NOSORT);

    DoubleClickedRowSignal.connect(
        boost::bind(&BasesListBox::BaseDoubleClicked, this, _1, _2, _3));
    LeftClickedRowSignal.connect(
        boost::bind(&BasesListBox::BaseLeftClicked, this, _1, _2, _3));
    MovedRowSignal.connect(
        boost::bind(&BasesListBox::QueueItemMoved, this, _1, _2));

    EnableOrderIssuing(false);
}

void BasesListBox::SizeMove(const GG::Pt& ul, const GG::Pt& lr) {
    const GG::Pt old_size = Size();
    CUIListBox::SizeMove(ul, lr);
    if (old_size != Size()) {
        const GG::Pt row_size = ListRowSize();
        for (auto& row : *this)
            row->Resize(row_size);
    }
}

void BasesListBox::ChildrenDraggedAway(const std::vector<GG::Wnd*>& wnds, const GG::Wnd* destination) {
    if (MatchesOrContains(this, destination))
        return;
    if (wnds.empty())
        return;
    if (wnds.size() != 1)
        ErrorLogger() << "BasesListBox::ChildrenDraggedAway unexpected informed that multiple Wnds were dragged away...";
    const GG::Wnd* wnd = wnds.front();  // should only be one wnd in list as BasesListBost doesn't allow selection, so dragging is only one-at-a-time
    const auto control = dynamic_cast<const GG::Control*>(wnd);
    if (!control)
        return;

    Row* original_row = boost::polymorphic_downcast<Row*>(*wnds.begin());
    iterator insertion_point = std::find_if(
        begin(), end(), [&original_row](const std::shared_ptr<Row>& xx){return xx.get() == original_row;});
    if (insertion_point != end())
        ++insertion_point;

    // replace dragged-away control with new copy
    auto row = ChildrenDraggedAwayCore(wnd);
    if (row) {
        Insert(row, insertion_point);
        row->Resize(ListRowSize());
    }

    // remove dragged-away row from this ListBox
    CUIListBox::ChildrenDraggedAway(wnds, destination);
    DetachChild(wnds.front());
}

void BasesListBox::SetEmpireShown(int empire_id, bool refresh_list) {
    m_empire_id_shown = empire_id;

    // disconnect old signal
    m_empire_designs_changed_signal.disconnect();

    // connect signal to update this list if the empire's designs change
    if (const Empire* empire = GetEmpire(m_empire_id_shown))
        m_empire_designs_changed_signal = empire->ShipDesignsChangedSignal.connect(
                                            boost::bind(&BasesListBox::Populate, this));

    if (refresh_list)
        Populate();
}

void BasesListBox::Populate() {
    DebugLogger() << "BasesListBox::Populate";

    // Provide conditional reminder text when the list is empty
    if (AvailabilityState().GetAvailabilities() == AvailabilityManager::DisplayedAvailabilies(false, false, false))
        SetEmptyPromptText(UserString("ALL_AVAILABILITY_FILTERS_BLOCKING_PROMPT"));
    else
        this->ResetEmptyListPrompt();

    // make note of first visible row to preserve state
    auto init_first_row_shown = FirstRowShown();
    std::size_t init_first_row_offset = std::distance(begin(), init_first_row_shown);

    Clear();

    this->PopulateCore();

    if (!Empty())
        BringRowIntoView(--end());
    if (init_first_row_offset < NumRows())
        BringRowIntoView(std::next(begin(), init_first_row_offset));

}

GG::Pt BasesListBox::ListRowSize()
{ return GG::Pt(Width() - ClientUI::ScrollWidth() - 5, BASES_LIST_BOX_ROW_HEIGHT); }

void BasesListBox::InitRowSizes() {
    // preinitialize listbox/row column widths, because what
    // ListBox::Insert does on default is not suitable for this case
    ManuallyManageColProps();
    NormalizeRowsOnInsert(false);
}

void BasesListBox::ItemRightClickedImpl(GG::ListBox::iterator it, const GG::Pt& pt, const GG::Flags<GG::ModKey>& modkeys)
{ this->BaseRightClicked(it, pt, modkeys); }

//////////////////////////////////////////////////
// BasesListBox derived classes                 //
//////////////////////////////////////////////////

class EmptyHullsListBox : public BasesListBox {
    public:
    EmptyHullsListBox(const AvailabilityManager& availabilities_state,
                      const boost::optional<std::string>& drop_type = boost::none) :
        BasesListBox::BasesListBox(availabilities_state, drop_type, UserString("ALL_AVAILABILITY_FILTERS_BLOCKING_PROMPT"))
    {};

    void EnableOrderIssuing(bool enable = true) override;

    protected:
    void PopulateCore() override;
    std::shared_ptr<Row> ChildrenDraggedAwayCore(const GG::Wnd* const wnd) override;
    void QueueItemMoved(const GG::ListBox::iterator& row_it, const GG::ListBox::iterator& original_position_it) override;

    void  BaseDoubleClicked(GG::ListBox::iterator it, const GG::Pt& pt, const GG::Flags<GG::ModKey>& modkeys) override;
    void  BaseLeftClicked(GG::ListBox::iterator it, const GG::Pt& pt, const GG::Flags<GG::ModKey>& modkeys) override;
    void  BaseRightClicked(GG::ListBox::iterator it, const GG::Pt& pt, const GG::Flags<GG::ModKey>& modkeys) override;

    private:
};

class CompletedDesignsListBox : public BasesListBox {
    public:
    CompletedDesignsListBox(const AvailabilityManager& availabilities_state,
                            const boost::optional<std::string>& drop_type = boost::none) :
        BasesListBox::BasesListBox(availabilities_state, drop_type)
    {};

    protected:
    void PopulateCore() override;
    void ResetEmptyListPrompt() override;
    std::shared_ptr<Row> ChildrenDraggedAwayCore(const GG::Wnd* const wnd) override;
    void QueueItemMoved(const GG::ListBox::iterator& row_it, const GG::ListBox::iterator& original_position_it) override;

    void  BaseDoubleClicked(GG::ListBox::iterator it, const GG::Pt& pt, const GG::Flags<GG::ModKey>& modkeys) override;
    void  BaseLeftClicked(GG::ListBox::iterator it, const GG::Pt& pt, const GG::Flags<GG::ModKey>& modkeys) override;
    void  BaseRightClicked(GG::ListBox::iterator it, const GG::Pt& pt, const GG::Flags<GG::ModKey>& modkeys) override;
};

class SavedDesignsListBox : public BasesListBox {
    public:
    SavedDesignsListBox(const AvailabilityManager& availabilities_state,
                        const boost::optional<std::string>& drop_type = boost::none) :
        BasesListBox::BasesListBox(availabilities_state, drop_type, UserString("ADD_FIRST_SAVED_DESIGN_QUEUE_PROMPT"))
    {};

    class SavedDesignListBoxRow : public BasesListBoxRow {
        public:
        SavedDesignListBoxRow(GG::X w, GG::Y h, const ShipDesign& design);
        void CompleteConstruction() override;
        const boost::uuids::uuid        DesignUUID() const;
        const std::string&              DesignName() const;
        const std::string&              Description() const;
        bool                            LookupInStringtable() const;

        private:
        boost::uuids::uuid              m_design_uuid;
    };

    protected:
    void PopulateCore() override;
    void ResetEmptyListPrompt() override;
    std::shared_ptr<Row> ChildrenDraggedAwayCore(const GG::Wnd* const wnd) override;
    void QueueItemMoved(const GG::ListBox::iterator& row_it, const GG::ListBox::iterator& original_position_it) override;

    void  BaseDoubleClicked(GG::ListBox::iterator it, const GG::Pt& pt, const GG::Flags<GG::ModKey>& modkeys) override;
    void  BaseLeftClicked(GG::ListBox::iterator it, const GG::Pt& pt, const GG::Flags<GG::ModKey>& modkeys) override;
    void  BaseRightClicked(GG::ListBox::iterator it, const GG::Pt& pt, const GG::Flags<GG::ModKey>& modkeys) override;
};

class MonstersListBox : public BasesListBox {
    public:
    MonstersListBox(const AvailabilityManager& availabilities_state,
                    const boost::optional<std::string>& drop_type = boost::none) :
        BasesListBox::BasesListBox(availabilities_state, drop_type)
    {};

    void EnableOrderIssuing(bool enable = true) override;

    protected:
    void PopulateCore() override;
    std::shared_ptr<Row> ChildrenDraggedAwayCore(const GG::Wnd* const wnd) override;

    void BaseDoubleClicked(GG::ListBox::iterator it, const GG::Pt& pt, const GG::Flags<GG::ModKey>& modkeys) override;
};


void EmptyHullsListBox::PopulateCore() {
    ScopedTimer scoped_timer("EmptyHulls::PopulateCore");
    DebugLogger() << "EmptyHulls::PopulateCore EmpireID(): " << EmpireID();

    const GG::Pt row_size = ListRowSize();

    const auto& manager = GetCurrentDesignsManager();

    for (const auto& hull_name : manager.OrderedHulls()) {
        const auto& hull_type =  GetHullTypeManager().GetHullType(hull_name);

        if (!hull_type || !hull_type->Producible())
            continue;

        auto shown = AvailabilityState().DisplayedHullAvailability(hull_name);
        if (!shown)
            continue;
        const std::vector<std::string> empty_policys_vec;
        auto row = GG::Wnd::Create<HullAndPolicysListBoxRow>(row_size.x, row_size.y, hull_name, empty_policys_vec);
        row->SetAvailability(*shown);
        Insert(row);
        row->Resize(row_size);
    }
}

void CompletedDesignsListBox::PopulateCore() {
    ScopedTimer scoped_timer("CompletedDesignsListBox::PopulateCore");

    const bool showing_available = AvailabilityState().GetAvailability(Availability::Available);

    const Universe& universe = GetUniverse();

    DebugLogger() << "CompletedDesignsListBox::PopulateCore for empire " << EmpireID();

    const GG::Pt row_size = ListRowSize();

    if (const auto empire = GetEmpire(EmpireID())) {
        // add rows for designs this empire is keeping
        const auto& manager = GetCurrentDesignsManager();
        for (int design_id : manager.AllOrderedIDs()) {
            try {
                const ShipDesign* design = GetShipDesign(design_id);
                if (!design)
                    continue;

                auto shown = AvailabilityState().DisplayedDesignAvailability(*design);
                if (shown) {
                    auto row = GG::Wnd::Create<CompletedDesignListBoxRow>(row_size.x, row_size.y, *design);
                    row->SetAvailability(*shown);
                    Insert(row);
                    row->Resize(row_size);
                }
            } catch (const std::out_of_range&) {
                ErrorLogger() << "ship design with id " << design_id << " incorrectly stored in manager.";
            }
        }
    } else if (showing_available) {
        // add all known / existing designs
        for (auto it = universe.beginShipDesigns();
             it != universe.endShipDesigns(); ++it)
        {
            const ShipDesign* design = it->second;
            if (!design->Producible())
                continue;
            auto row = GG::Wnd::Create<CompletedDesignListBoxRow>(row_size.x, row_size.y, *design);
            Insert(row);
            row->Resize(row_size);
        }
    }
}

void SavedDesignsListBox::PopulateCore() {
    ScopedTimer scoped_timer("CompletedDesigns::PopulateCore");
    DebugLogger() << "CompletedDesigns::PopulateCore";

    const GG::Pt row_size = ListRowSize();

    for (const auto& uuid : GetSavedDesignsManager().OrderedDesignUUIDs()) {
        const auto design = GetSavedDesignsManager().GetDesign(uuid);
        auto shown = AvailabilityState().DisplayedDesignAvailability(*design);
        if (!shown)
            continue;

        auto row = GG::Wnd::Create<SavedDesignListBoxRow>(row_size.x, row_size.y, *design);
        Insert(row);
        row->Resize(row_size);
        row->SetAvailability(*shown);
    }
}

void MonstersListBox::PopulateCore() {
    ScopedTimer scoped_timer("Monsters::PopulateCore");

    const Universe& universe = GetUniverse();

    const GG::Pt row_size = ListRowSize();

    for (auto it = universe.beginShipDesigns();
         it != universe.endShipDesigns(); ++it)
    {
        const ShipDesign* design = it->second;
        if (!design->IsMonster())
            continue;
        auto row = GG::Wnd::Create<CompletedDesignListBoxRow>(row_size.x, row_size.y,
                                                              *design);
        Insert(row);
        row->Resize(row_size);
    }
}


void BasesListBox::ResetEmptyListPrompt()
{ SetEmptyPromptText(UserString("ALL_AVAILABILITY_FILTERS_BLOCKING_PROMPT")); }

void CompletedDesignsListBox::ResetEmptyListPrompt() {
    if (!GetOptionsDB().Get<bool>("resource.shipdesign.saved.enabled")
        && !GetOptionsDB().Get<bool>("resource.shipdesign.default.enabled"))
    {
        SetEmptyPromptText(UserString("NO_SAVED_OR_DEFAULT_DESIGNS_ADDED_PROMPT"));
    } else {
        SetEmptyPromptText(UserString("ADD_FIRST_DESIGN_DESIGN_QUEUE_PROMPT"));
    }

}

void SavedDesignsListBox::ResetEmptyListPrompt()
{ SetEmptyPromptText(UserString("ADD_FIRST_SAVED_DESIGN_QUEUE_PROMPT")); }


std::shared_ptr<BasesListBox::Row> EmptyHullsListBox::ChildrenDraggedAwayCore(const GG::Wnd* const wnd) {
    // find type of hull that was dragged away, and replace
    const auto design_row = dynamic_cast<const BasesListBox::HullAndPolicysListBoxRow*>(wnd);
    if (!design_row)
        return nullptr;

    const std::string& hull_name = design_row->Hull();
    const auto row_size = ListRowSize();
    std::vector<std::string> empty_policys_vec;
    auto row =  GG::Wnd::Create<HullAndPolicysListBoxRow>(row_size.x, row_size.y, hull_name, empty_policys_vec);

    if (auto shown = AvailabilityState().DisplayedHullAvailability(hull_name))
        row->SetAvailability(*shown);

    return row;
}

std::shared_ptr<BasesListBox::Row> CompletedDesignsListBox::ChildrenDraggedAwayCore(const GG::Wnd* const wnd) {
    // find design that was dragged away, and replace

    const auto design_row = dynamic_cast<const BasesListBox::CompletedDesignListBoxRow*>(wnd);
    if (!design_row)
        return nullptr;

    int design_id = design_row->DesignID();
    const ShipDesign* design = GetShipDesign(design_id);
    if (!design) {
        ErrorLogger() << "Missing design with id " << design_id;
        return nullptr;
    }

    const auto row_size = ListRowSize();
    auto row = GG::Wnd::Create<CompletedDesignListBoxRow>(row_size.x, row_size.y, *design);
    if (auto shown = AvailabilityState().DisplayedDesignAvailability(*design))
        row->SetAvailability(*shown);
    return row;
}

std::shared_ptr<BasesListBox::Row> SavedDesignsListBox::ChildrenDraggedAwayCore(const GG::Wnd* const wnd) {
    // find name of design that was dragged away, and replace
    const auto design_row = dynamic_cast<const SavedDesignsListBox::SavedDesignListBoxRow*>(wnd);
    if (!design_row)
        return nullptr;

    SavedDesignsManager& manager = GetSavedDesignsManager();
    const auto design = manager.GetDesign(design_row->DesignUUID());
    if (!design) {
        ErrorLogger() << "Saved design missing with uuid " << design_row->DesignUUID();
        return nullptr;
    }

    const auto row_size = ListRowSize();
    auto row = GG::Wnd::Create<SavedDesignListBoxRow>(row_size.x, row_size.y, *design);

    if (auto shown = AvailabilityState().DisplayedDesignAvailability(*design))
        row->SetAvailability(*shown);

    return row;
}

std::shared_ptr<BasesListBox::Row> MonstersListBox::ChildrenDraggedAwayCore(const GG::Wnd* const wnd) {
    // Replace the design that was dragged away
    const auto design_row = dynamic_cast<const BasesListBox::CompletedDesignListBoxRow*>(wnd);
    if (!design_row)
        return nullptr;

    int design_id = design_row->DesignID();
    const ShipDesign* design = GetShipDesign(design_id);
    if (!design) {
        ErrorLogger() << "Missing design with id " << design_id;
        return nullptr;
    }

    const auto row_size = ListRowSize();
    auto row = GG::Wnd::Create<CompletedDesignListBoxRow>(row_size.x, row_size.y, *design);
    return row;
}


void EmptyHullsListBox::EnableOrderIssuing(bool enable/* = true*/)
{ QueueListBox::EnableOrderIssuing(enable); }

void MonstersListBox::EnableOrderIssuing(bool)
{ QueueListBox::EnableOrderIssuing(false); }


void EmptyHullsListBox::BaseDoubleClicked(GG::ListBox::iterator it, const GG::Pt& pt,
                                          const GG::Flags<GG::ModKey>& modkeys)
{
    const auto hp_row = dynamic_cast<HullAndPolicysListBoxRow*>(it->get());
    if (!hp_row)
        return;

    if (!hp_row->Hull().empty() || !hp_row->Policys().empty())
        DesignComponentsSelectedSignal(hp_row->Hull(), hp_row->Policys());
}

void CompletedDesignsListBox::BaseDoubleClicked(GG::ListBox::iterator it, const GG::Pt& pt,
                                                const GG::Flags<GG::ModKey>& modkeys)
{
    const auto cd_row = dynamic_cast<CompletedDesignListBoxRow*>(it->get());
    if (!cd_row || cd_row->DesignID() == INVALID_DESIGN_ID)
        return;

    DesignSelectedSignal(cd_row->DesignID());
}

void SavedDesignsListBox::BaseDoubleClicked(GG::ListBox::iterator it, const GG::Pt& pt,
                                            const GG::Flags<GG::ModKey>& modkeys)
{
    const auto sd_row = dynamic_cast<SavedDesignListBoxRow*>(it->get());

    if (!sd_row)
        return;
    SavedDesignSelectedSignal(sd_row->DesignUUID());
}

void MonstersListBox::BaseDoubleClicked(GG::ListBox::iterator it, const GG::Pt& pt,
                                        const GG::Flags<GG::ModKey>& modkeys)
{
    const auto cd_row = dynamic_cast<CompletedDesignListBoxRow*>(it->get());
    if (!cd_row || cd_row->DesignID() == INVALID_DESIGN_ID)
        return;

    DesignSelectedSignal(cd_row->DesignID());
}


void EmptyHullsListBox::BaseLeftClicked(GG::ListBox::iterator it, const GG::Pt& pt,
                                        const GG::Flags<GG::ModKey>& modkeys)
{
    const auto hull_policys_row = dynamic_cast<HullAndPolicysListBoxRow*>(it->get());
    if (!hull_policys_row)
        return;
    const std::string& hull_name = hull_policys_row->Hull();
    const HullType* hull_type = GetHullType(hull_name);
    const std::vector<std::string>& policys = hull_policys_row->Policys();

    if (modkeys & GG::MOD_KEY_CTRL) {
        // Toggle hull obsolete
        auto& manager = GetCurrentDesignsManager();
        const auto is_obsolete = manager.IsHullObsolete(hull_name);
        manager.SetHullObsolete(hull_name, !is_obsolete);
        Populate();
    }
    else if (hull_type && policys.empty())
        HullClickedSignal(hull_type);
}

void CompletedDesignsListBox::BaseLeftClicked(GG::ListBox::iterator it, const GG::Pt& pt,
                                              const GG::Flags<GG::ModKey>& modkeys)
{
    const auto design_row = dynamic_cast<CompletedDesignListBoxRow*>(it->get());
    if (!design_row)
        return;
    int id = design_row->DesignID();
    const ShipDesign* design = GetShipDesign(id);
    if (!design)
        return;
    const auto& manager = GetCurrentDesignsManager();
    if (modkeys & GG::MOD_KEY_CTRL && manager.IsKnown(id)) {
        const auto maybe_obsolete = manager.IsObsolete(id);
        bool is_obsolete = maybe_obsolete && *maybe_obsolete;
        SetObsoleteInCurrentDesigns(id, !is_obsolete);
        Populate();
    }
    else
        DesignClickedSignal(design);
}

void SavedDesignsListBox::BaseLeftClicked(GG::ListBox::iterator it, const GG::Pt& pt,
                                          const GG::Flags<GG::ModKey>& modkeys)
{
    const auto saved_design_row = dynamic_cast<SavedDesignListBoxRow*>(it->get());
    if (!saved_design_row)
        return;
    const auto design_uuid = saved_design_row->DesignUUID();
    auto& manager = GetSavedDesignsManager();
    const auto design = manager.GetDesign(design_uuid);
    if (!design)
        return;
    if (modkeys & GG::MOD_KEY_CTRL)
        AddSavedDesignToCurrentDesigns(design->UUID(), EmpireID());
    else 
        DesignClickedSignal(design);
}


void EmptyHullsListBox::BaseRightClicked(GG::ListBox::iterator it, const GG::Pt& pt,
                                         const GG::Flags<GG::ModKey>& modkeys)
{
    const auto hull_policys_row = dynamic_cast<HullAndPolicysListBoxRow*>(it->get());
    if (!hull_policys_row)
        return;
    const std::string& hull_name = hull_policys_row->Hull();

    // Context menu actions
    auto& manager = GetCurrentDesignsManager();
    auto is_obsolete = manager.IsHullObsolete(hull_name);
    auto toggle_obsolete_design_action = [&manager, &hull_name, is_obsolete, this]() {
        manager.SetHullObsolete(hull_name, !is_obsolete);
        Populate();
    };

    // create popup menu with a commands in it
    auto popup = GG::Wnd::Create<CUIPopupMenu>(pt.x, pt.y);

    const auto empire_id = EmpireID();
    if (empire_id != ALL_EMPIRES)
        popup->AddMenuItem(GG::MenuItem(
                               (is_obsolete
                                ? UserString("DESIGN_WND_UNOBSOLETE_HULL")
                                : UserString("DESIGN_WND_OBSOLETE_HULL")),
                               false, false, toggle_obsolete_design_action));

    popup->Run();
}

void CompletedDesignsListBox::BaseRightClicked(GG::ListBox::iterator it, const GG::Pt& pt,
                                               const GG::Flags<GG::ModKey>& modkeys)
{
    const auto design_row = dynamic_cast<CompletedDesignListBoxRow*>(it->get());
    if (!design_row)
        return;

    const auto design_id = design_row->DesignID();
    const auto design = GetShipDesign(design_id);
    if (!design)
        return;

    DesignRightClickedSignal(design);

    const auto empire_id = EmpireID();

    DebugLogger() << "BasesListBox::BaseRightClicked on design id : " << design_id;

    if (design->UUID() == boost::uuids::uuid{{0}})
        ErrorLogger() << "Already nil";

    // Context menu actions
    const auto& manager = GetCurrentDesignsManager();
    const auto maybe_obsolete = manager.IsObsolete(design_id);
    bool is_obsolete = maybe_obsolete && *maybe_obsolete;
    auto toggle_obsolete_design_action = [&design_id, is_obsolete, this]() {
        SetObsoleteInCurrentDesigns(design_id, !is_obsolete);
        Populate();
    };

    auto delete_design_action = [&design_id, this]() {
        DeleteFromCurrentDesigns(design_id);
        Populate();
    };
    
    auto rename_design_action = [&empire_id, &design_id, design, &design_row]() {
        auto edit_wnd = GG::Wnd::Create<CUIEditWnd>(GG::X(350), UserString("DESIGN_ENTER_NEW_DESIGN_NAME"), design->Name());
        edit_wnd->Run();
        const std::string& result = edit_wnd->Result();
        if (result != "" && result != design->Name()) {
            HumanClientApp::GetApp()->Orders().IssueOrder(
                std::make_shared<ShipDesignOrder>(empire_id, design_id, result));
            design_row->SetDisplayName(design->Name());
        }
    };

    auto save_design_action = [&design]() {
        auto saved_design = *design;
        saved_design.SetUUID(boost::uuids::random_generator()());
        GetSavedDesignsManager().InsertBefore(saved_design, GetSavedDesignsManager().OrderedDesignUUIDs().begin());
    };

    // toggle the option to add all saved designs at game start.
    const auto add_defaults = GetOptionsDB().Get<bool>("resource.shipdesign.default.enabled");
    auto toggle_add_default_designs_at_game_start_action = [add_defaults]() {
        GetOptionsDB().Set<bool>("resource.shipdesign.default.enabled", !add_defaults);
    };

    // create popup menu with a commands in it
    auto popup = GG::Wnd::Create<CUIPopupMenu>(pt.x, pt.y);

    // obsolete design
    if (empire_id != ALL_EMPIRES)
        popup->AddMenuItem(GG::MenuItem(
                              (is_obsolete
                               ? UserString("DESIGN_WND_UNOBSOLETE_DESIGN")
                               : UserString("DESIGN_WND_OBSOLETE_DESIGN")),
                              false, false, toggle_obsolete_design_action));

    // delete design
    if (empire_id != ALL_EMPIRES)
        popup->AddMenuItem(GG::MenuItem(UserString("DESIGN_WND_DELETE_DESIGN"), false, false, delete_design_action));

    // rename design
    if (design->DesignedByEmpire() == empire_id)
        popup->AddMenuItem(GG::MenuItem(UserString("DESIGN_RENAME"), false, false, rename_design_action));

    // save design
    popup->AddMenuItem(GG::MenuItem(UserString("DESIGN_SAVE"), false, false, save_design_action));

    popup->AddMenuItem(GG::MenuItem(true)); // separator
    popup->AddMenuItem(GG::MenuItem(UserString("DESIGN_WND_ADD_ALL_DEFAULT_START"), false, add_defaults,
                                    toggle_add_default_designs_at_game_start_action));

    popup->Run();
}

void SavedDesignsListBox::BaseRightClicked(GG::ListBox::iterator it, const GG::Pt& pt,
                                           const GG::Flags<GG::ModKey>& modkeys)
{
    const auto design_row = dynamic_cast<SavedDesignListBoxRow*>(it->get());
    if (!design_row)
        return;
    const auto design_uuid = design_row->DesignUUID();
    auto& manager = GetSavedDesignsManager();
    const auto design = manager.GetDesign(design_uuid);
    if (!design)
        return;
    const auto empire_id = EmpireID();

    DesignRightClickedSignal(design);

    DebugLogger() << "BasesListBox::BaseRightClicked on design name : " << design->Name();

    // Context menu actions
    // add design to empire
    auto add_design_action = [&manager, &design, &empire_id]() {
        AddSavedDesignToCurrentDesigns(design->UUID(), empire_id);
    };
    
    // delete design from saved designs 
    auto delete_saved_design_action = [&manager, &design, this]() {
        DebugLogger() << "BasesListBox::BaseRightClicked Delete Saved Design" << design->Name();
        manager.Erase(design->UUID());
        Populate();
    };

    // add all saved designs
    auto add_all_saved_designs_action = [&manager, &empire_id]() {
        DebugLogger() << "BasesListBox::BaseRightClicked AddAllSavedDesignsToCurrentDesigns";
        for (const auto& uuid : manager.OrderedDesignUUIDs())
            AddSavedDesignToCurrentDesigns(uuid, empire_id);
    };

    // toggle the option to add all saved designs at game start.
    const auto add_all = GetOptionsDB().Get<bool>("resource.shipdesign.saved.enabled");
    auto toggle_add_all_saved_game_start_action = [add_all]() {
        GetOptionsDB().Set<bool>("resource.shipdesign.saved.enabled", !add_all);
    };

    
    // create popup menu with a commands in it
    auto popup = GG::Wnd::Create<CUIPopupMenu>(pt.x, pt.y);
    if (design->Producible())
        popup->AddMenuItem(GG::MenuItem(UserString("DESIGN_ADD"), false, false, add_design_action));
    popup->AddMenuItem(GG::MenuItem(UserString("DESIGN_WND_DELETE_SAVED"), false, false, delete_saved_design_action));
    popup->AddMenuItem(GG::MenuItem(UserString("DESIGN_WND_ADD_ALL_SAVED_NOW"), false, false, add_all_saved_designs_action));
    popup->AddMenuItem(GG::MenuItem(true)); // separator
    popup->AddMenuItem(GG::MenuItem(UserString("DESIGN_WND_ADD_ALL_SAVED_START"), false, add_all,
                                   toggle_add_all_saved_game_start_action));

    popup->Run();

}

void EmptyHullsListBox::QueueItemMoved(const GG::ListBox::iterator& row_it,
                                       const GG::ListBox::iterator& original_position_it)
{
    const auto control = dynamic_cast<HullAndPolicysListBoxRow*>(row_it->get());
    if (!control || !GetEmpire(EmpireID()))
        return;

    const std::string& hull_name = control->Hull();

    iterator insert_before_row = std::next(row_it);

    const auto insert_before_control = (insert_before_row == end()) ? nullptr :
        boost::polymorphic_downcast<const HullAndPolicysListBoxRow*>(insert_before_row->get());
    std::string insert_before_hull = insert_before_control
        ? insert_before_control->Hull() : "";

    control->Resize(ListRowSize());

    GetCurrentDesignsManager().InsertHullBefore(hull_name, insert_before_hull);
}

void CompletedDesignsListBox::QueueItemMoved(const GG::ListBox::iterator& row_it,
                                             const GG::ListBox::iterator& original_position_it)
{
    const auto control = dynamic_cast<BasesListBox::CompletedDesignListBoxRow*>(row_it->get());
    if (!control || !GetEmpire(EmpireID()))
        return;

    int design_id = control->DesignID();

    iterator insert_before_row = std::next(row_it);

    const auto insert_before_control = (insert_before_row == end()) ? nullptr :
        boost::polymorphic_downcast<const BasesListBox::CompletedDesignListBoxRow*>(insert_before_row->get());
    int insert_before_id = insert_before_control
        ? insert_before_control->DesignID() : INVALID_DESIGN_ID;

    control->Resize(ListRowSize());

    GetCurrentDesignsManager().MoveBefore(design_id, insert_before_id);
}

void SavedDesignsListBox::QueueItemMoved(const GG::ListBox::iterator& row_it,
                                         const GG::ListBox::iterator& original_position_it)
{
    const auto control = dynamic_cast<SavedDesignsListBox::SavedDesignListBoxRow*>(row_it->get());
    if (!control)
        return;

    const auto& uuid = control->DesignUUID();

    iterator insert_before_row = std::next(row_it);

    const auto insert_before_control = (insert_before_row == end()) ? nullptr :
        boost::polymorphic_downcast<const SavedDesignsListBox::SavedDesignListBoxRow*>(insert_before_row->get());
    const auto& next_uuid = insert_before_control
        ? insert_before_control->DesignUUID() : boost::uuids::uuid{{0}};

    if (GetSavedDesignsManager().MoveBefore(uuid, next_uuid))
        control->Resize(ListRowSize());
}


//////////////////////////////////////////////////
// BasesListBox derived class rows              //
//////////////////////////////////////////////////

SavedDesignsListBox::SavedDesignListBoxRow::SavedDesignListBoxRow(
    GG::X w, GG::Y h, const ShipDesign& design) :
    BasesListBoxRow(w, h, design.Hull(), design.Name()),
    m_design_uuid(design.UUID())
{}

void SavedDesignsListBox::SavedDesignListBoxRow::CompleteConstruction() {
    BasesListBoxRow::CompleteConstruction();
    SetDragDropDataType(SAVED_DESIGN_ROW_DROP_STRING);
}

const boost::uuids::uuid SavedDesignsListBox::SavedDesignListBoxRow::DesignUUID() const {
    SavedDesignsManager& manager = GetSavedDesignsManager();
    const ShipDesign* design = manager.GetDesign(m_design_uuid);
    if (!design) {
        ErrorLogger() << "Saved ship design missing with uuid " << m_design_uuid;
        return boost::uuids::uuid{};
    }
    return design->UUID();
}

const std::string& SavedDesignsListBox::SavedDesignListBoxRow::DesignName() const {
    SavedDesignsManager& manager = GetSavedDesignsManager();
    const ShipDesign* design = manager.GetDesign(m_design_uuid);
    if (!design)
        return EMPTY_STRING;
    return design->Name();
}

const std::string& SavedDesignsListBox::SavedDesignListBoxRow::Description() const {
    SavedDesignsManager& manager = GetSavedDesignsManager();
    const ShipDesign* design = manager.GetDesign(m_design_uuid);
    if (!design)
        return EMPTY_STRING;
    return design->Description();
}

bool SavedDesignsListBox::SavedDesignListBoxRow::LookupInStringtable() const {
    SavedDesignsManager& manager = GetSavedDesignsManager();
    const ShipDesign* design = manager.GetDesign(m_design_uuid);
    if (!design)
        return false;
    return design->LookupInStringtable();
}



//////////////////////////////////////////////////
// DesignWnd::BaseSelector                      //
//////////////////////////////////////////////////
class DesignWnd::BaseSelector : public CUIWnd {
public:
    /** \name Structors */ //@{
    BaseSelector(const std::string& config_name);
    void CompleteConstruction() override;
    //@}

    /** \name Mutators */ //@{
    void SizeMove(const GG::Pt& ul, const GG::Pt& lr) override;

    void            Reset();
    void            ToggleAvailability(const Availability::Enum type);
    void            SetEmpireShown(int empire_id, bool refresh_list);
    void            EnableOrderIssuing(bool enable/* = true*/);
    //@}

    mutable boost::signals2::signal<void (int)>                 DesignSelectedSignal;
    mutable boost::signals2::signal<void (const std::string&, const std::vector<std::string>&)>
                                                                DesignComponentsSelectedSignal;
    mutable boost::signals2::signal<void (const boost::uuids::uuid&)>
                                                                SavedDesignSelectedSignal;

    mutable boost::signals2::signal<void (const ShipDesign*)>   DesignClickedSignal;
    mutable boost::signals2::signal<void (const HullType*)>     HullClickedSignal;

    enum class BaseSelectorTab : std::size_t {Hull, Current, Saved, Monster};
    mutable boost::signals2::signal<void (const BaseSelectorTab)> TabChangedSignal;

private:
    void            DoLayout();

    std::shared_ptr<GG::TabWnd>                m_tabs;
    std::shared_ptr<EmptyHullsListBox>         m_hulls_list;           // empty hulls on which a new design can be based
    std::shared_ptr<CompletedDesignsListBox>   m_designs_list;         // designs this empire has created or learned how to make
    std::shared_ptr<SavedDesignsListBox>       m_saved_designs_list;   // designs saved to files
    std::shared_ptr<MonstersListBox>           m_monsters_list;        // monster designs

    // Holds the state of the availabilities filter.
    AvailabilityManager m_availabilities_state;
    std::tuple<std::shared_ptr<CUIStateButton>, std::shared_ptr<CUIStateButton>, std::shared_ptr<CUIStateButton>> m_availabilities_buttons;
};

DesignWnd::BaseSelector::BaseSelector(const std::string& config_name) :
    CUIWnd(UserString("DESIGN_WND_STARTS"),
           GG::INTERACTIVE | GG::RESIZABLE | GG::ONTOP | GG::DRAGABLE | PINABLE,
           config_name),
    m_tabs(nullptr),
    m_hulls_list(nullptr),
    m_designs_list(nullptr),
    m_saved_designs_list(nullptr),
    m_monsters_list(nullptr),
    m_availabilities_state{false, true, false}
{}

void DesignWnd::BaseSelector::CompleteConstruction() {
    // TODO: C++17, Collect and replace with structured binding auto [a, b, c] = m_availabilities;
    auto& m_obsolete_button = std::get<Availability::Obsolete>(m_availabilities_buttons);
    m_obsolete_button = GG::Wnd::Create<CUIStateButton>(UserString("PRODUCTION_WND_AVAILABILITY_OBSOLETE"), GG::FORMAT_CENTER, std::make_shared<CUILabelButtonRepresenter>());
    AttachChild(m_obsolete_button);
    m_obsolete_button->CheckedSignal.connect(
        boost::bind(&DesignWnd::BaseSelector::ToggleAvailability, this, Availability::Obsolete));
    m_obsolete_button->SetCheck(m_availabilities_state.GetAvailability(Availability::Obsolete));

    auto& m_available_button = std::get<Availability::Available>(m_availabilities_buttons);
    m_available_button = GG::Wnd::Create<CUIStateButton>(UserString("PRODUCTION_WND_AVAILABILITY_AVAILABLE"), GG::FORMAT_CENTER, std::make_shared<CUILabelButtonRepresenter>());
    AttachChild(m_available_button);
    m_available_button->CheckedSignal.connect(
        boost::bind(&DesignWnd::BaseSelector::ToggleAvailability, this, Availability::Available));
    m_available_button->SetCheck(m_availabilities_state.GetAvailability(Availability::Available));

    auto& m_unavailable_button = std::get<Availability::Future>(m_availabilities_buttons);
    m_unavailable_button = GG::Wnd::Create<CUIStateButton>(UserString("PRODUCTION_WND_AVAILABILITY_UNAVAILABLE"), GG::FORMAT_CENTER, std::make_shared<CUILabelButtonRepresenter>());
    AttachChild(m_unavailable_button);
    m_unavailable_button->CheckedSignal.connect(
        boost::bind(&DesignWnd::BaseSelector::ToggleAvailability, this, Availability::Future));
    m_unavailable_button->SetCheck(m_availabilities_state.GetAvailability(Availability::Future));

    m_tabs = GG::Wnd::Create<GG::TabWnd>(GG::X(5), GG::Y(2), GG::X(10), GG::Y(10), ClientUI::GetFont(), ClientUI::WndColor(), ClientUI::TextColor());
    m_tabs->TabChangedSignal.connect(
        boost::bind(&DesignWnd::BaseSelector::Reset, this));
    AttachChild(m_tabs);

    m_hulls_list = GG::Wnd::Create<EmptyHullsListBox>(m_availabilities_state, HULL_PARTS_ROW_DROP_TYPE_STRING);
    m_hulls_list->Resize(GG::Pt(GG::X(10), GG::Y(10)));
    m_tabs->AddWnd(m_hulls_list, UserString("DESIGN_WND_HULLS"));
    m_hulls_list->DesignComponentsSelectedSignal.connect(
        DesignWnd::BaseSelector::DesignComponentsSelectedSignal);
    m_hulls_list->HullClickedSignal.connect(
        DesignWnd::BaseSelector::HullClickedSignal);

    m_designs_list = GG::Wnd::Create<CompletedDesignsListBox>(m_availabilities_state, COMPLETE_DESIGN_ROW_DROP_STRING);
    m_designs_list->Resize(GG::Pt(GG::X(10), GG::Y(10)));
    m_tabs->AddWnd(m_designs_list, UserString("DESIGN_WND_FINISHED_DESIGNS"));
    m_designs_list->DesignSelectedSignal.connect(
        DesignWnd::BaseSelector::DesignSelectedSignal);
    m_designs_list->DesignClickedSignal.connect(
        DesignWnd::BaseSelector::DesignClickedSignal);

    m_saved_designs_list = GG::Wnd::Create<SavedDesignsListBox>(m_availabilities_state, SAVED_DESIGN_ROW_DROP_STRING);
    m_saved_designs_list->Resize(GG::Pt(GG::X(10), GG::Y(10)));
    m_tabs->AddWnd(m_saved_designs_list, UserString("DESIGN_WND_SAVED_DESIGNS"));
    m_saved_designs_list->SavedDesignSelectedSignal.connect(
        DesignWnd::BaseSelector::SavedDesignSelectedSignal);
    m_saved_designs_list->DesignClickedSignal.connect(
        DesignWnd::BaseSelector::DesignClickedSignal);

    m_monsters_list = GG::Wnd::Create<MonstersListBox>(m_availabilities_state);
    m_monsters_list->Resize(GG::Pt(GG::X(10), GG::Y(10)));
    m_tabs->AddWnd(m_monsters_list, UserString("DESIGN_WND_MONSTERS"));
    m_monsters_list->DesignSelectedSignal.connect(
        DesignWnd::BaseSelector::DesignSelectedSignal);
    m_monsters_list->DesignClickedSignal.connect(
        DesignWnd::BaseSelector::DesignClickedSignal);

    CUIWnd::CompleteConstruction();

    DoLayout();
    SaveDefaultedOptions();
}

void DesignWnd::BaseSelector::SizeMove(const GG::Pt& ul, const GG::Pt& lr) {
    const GG::Pt old_size = Size();
    CUIWnd::SizeMove(ul, lr);
    if (old_size != Size())
        DoLayout();
}

void DesignWnd::BaseSelector::Reset() {
    ScopedTimer scoped_timer("BaseSelector::Reset");

    const int empire_id = HumanClientApp::GetApp()->EmpireID();
    SetEmpireShown(empire_id, false);

    if (auto base_box = dynamic_cast<BasesListBox*>(m_tabs->CurrentWnd()))
        base_box->Populate();

    // Signal the type of tab selected
    auto tab_type = BaseSelectorTab(m_tabs->CurrentWndIndex());
    switch (tab_type) {
    case BaseSelectorTab::Hull:
    case BaseSelectorTab::Current:
    case BaseSelectorTab::Saved:
    case BaseSelectorTab::Monster:
        TabChangedSignal(tab_type);
        break;
    default:
        break;
    }
}

void DesignWnd::BaseSelector::SetEmpireShown(int empire_id, bool refresh_list) {
    m_hulls_list->SetEmpireShown(empire_id, refresh_list);
    m_designs_list->SetEmpireShown(empire_id, refresh_list);
    m_saved_designs_list->SetEmpireShown(empire_id, refresh_list);
}

void DesignWnd::BaseSelector::ToggleAvailability(Availability::Enum type) {
    std::shared_ptr<CUIStateButton> button;
    bool state = false;
    switch (type) {
    case Availability::Obsolete:
        m_availabilities_state.ToggleAvailability(Availability::Obsolete);
        state = m_availabilities_state.GetAvailability(Availability::Obsolete);
        button = std::get<Availability::Obsolete>(m_availabilities_buttons);
        break;
    case Availability::Available:
        m_availabilities_state.ToggleAvailability(Availability::Available);
        state = m_availabilities_state.GetAvailability(Availability::Available);
        button = std::get<Availability::Available>(m_availabilities_buttons);
        break;
    case Availability::Future:
        m_availabilities_state.ToggleAvailability(Availability::Future);
        state = m_availabilities_state.GetAvailability(Availability::Future);
        button = std::get<Availability::Future>(m_availabilities_buttons);
        break;
    }

    button->SetCheck(state);

    m_hulls_list->Populate();
    m_designs_list->Populate();
    m_saved_designs_list->Populate();
}

void DesignWnd::BaseSelector::EnableOrderIssuing(bool enable/* = true*/) {
    m_hulls_list->EnableOrderIssuing(enable);
    m_designs_list->EnableOrderIssuing(enable);
    m_saved_designs_list->EnableOrderIssuing(enable);
    m_monsters_list->EnableOrderIssuing(enable);
}

void DesignWnd::BaseSelector::DoLayout() {
    const GG::X LEFT_PAD(5);
    const GG::Y TOP_PAD(2);
    const GG::X AVAILABLE_WIDTH = ClientWidth() - 2*LEFT_PAD;
    const int BUTTON_SEPARATION = 3;
    const GG::X BUTTON_WIDTH = (AVAILABLE_WIDTH - 2*BUTTON_SEPARATION) / 3;
    const int PTS = ClientUI::Pts();
    const GG::Y BUTTON_HEIGHT(PTS * 2);

    GG::Y top(TOP_PAD);
    GG::X left(LEFT_PAD);

    // TODO: C++17, Replace with structured binding auto [a, b, c] = m_availabilities;
    auto& m_obsolete_button = std::get<Availability::Obsolete>(m_availabilities_buttons);
    auto& m_available_button = std::get<Availability::Available>(m_availabilities_buttons);
    auto& m_unavailable_button = std::get<Availability::Future>(m_availabilities_buttons);

    m_obsolete_button->SizeMove(GG::Pt(left, top), GG::Pt(left + BUTTON_WIDTH, top + BUTTON_HEIGHT));
    left = left + BUTTON_WIDTH + BUTTON_SEPARATION;
    m_available_button->SizeMove(GG::Pt(left, top), GG::Pt(left + BUTTON_WIDTH, top + BUTTON_HEIGHT));
    left = left + BUTTON_WIDTH + BUTTON_SEPARATION;
    m_unavailable_button->SizeMove(GG::Pt(left, top), GG::Pt(left + BUTTON_WIDTH, top + BUTTON_HEIGHT));
    left = LEFT_PAD;
    top = top + BUTTON_HEIGHT + BUTTON_SEPARATION;

    m_tabs->SizeMove(GG::Pt(left, top), ClientSize() - GG::Pt(LEFT_PAD, TOP_PAD));
}


//////////////////////////////////////////////////
// SlotControl                                  //
//////////////////////////////////////////////////
/** UI representation and drop-target for slots of a design.  PolicyControl may
  * be dropped into slots to add the corresponding policys to the ShipDesign, or
  * the policy may be set programmatically with SetPolicy(). */
class SlotControl : public GG::Control {
public:
    /** \name Structors */ //@{
    SlotControl();
    SlotControl(double x, double y, ShipSlotType slot_type);
    //@}
    void CompleteConstruction() override;

    /** \name Accessors */ //@{
    ShipSlotType    SlotType() const;
    double          XPositionFraction() const;
    double          YPositionFraction() const;
    const Policy* GetPolicy() const;
    //@}

    /** \name Mutators */ //@{
    void StartingChildDragDrop(const GG::Wnd* wnd, const GG::Pt& offset) override;

    void CancellingChildDragDrop(const std::vector<const GG::Wnd*>& wnds) override;

    void AcceptDrops(const GG::Pt& pt, std::vector<std::shared_ptr<GG::Wnd>> wnds, GG::Flags<GG::ModKey> mod_keys) override;

    void ChildrenDraggedAway(const std::vector<GG::Wnd*>& wnds, const GG::Wnd* destination) override;

    void DragDropEnter(const GG::Pt& pt, std::map<const Wnd*, bool>& drop_wnds_acceptable,
                       GG::Flags<GG::ModKey> mod_keys) override;

    void DragDropLeave() override;

    void Render() override;

    void            Highlight(bool actually = true);

    void            SetPolicy(const std::string& policy_name);  //!< used to programmatically set the PolicyControl in this slot.  Does not emit signal
    void            SetPolicy(const Policy* policy_type = nullptr); //!< used to programmatically set the PolicyControl in this slot.  Does not emit signal
    //@}

    /** emitted when the contents of a slot are altered by the dragging
      * a PolicyControl in or out of the slot.  signal should be caught and the
      * slot contents set using SetPolicy accordingly */
    mutable boost::signals2::signal<void (const Policy*, bool)> SlotContentsAlteredSignal;

    mutable boost::signals2::signal<void (const Policy*, GG::Flags<GG::ModKey>)> PolicyTypeClickedSignal;

protected:
    bool EventFilter(GG::Wnd* w, const GG::WndEvent& event) override;

    void DropsAcceptable(DropsAcceptableIter first, DropsAcceptableIter last,
                         const GG::Pt& pt, GG::Flags<GG::ModKey> mod_keys) const override;

private:
    bool                m_highlighted;
    ShipSlotType        m_slot_type;
    double              m_x_position_fraction, m_y_position_fraction;   //!< position on hull image where slot should be shown, as a fraction of that image's size
    std::shared_ptr<PolicyControl>        m_policy_control;
    std::shared_ptr<GG::StaticGraphic>  m_background;
};

SlotControl::SlotControl() :
    GG::Control(GG::X0, GG::Y0, SLOT_CONTROL_WIDTH, SLOT_CONTROL_HEIGHT, GG::INTERACTIVE),
    m_highlighted(false),
    m_slot_type(INVALID_SHIP_SLOT_TYPE),
    m_x_position_fraction(0.4),
    m_y_position_fraction(0.4),
    m_policy_control(nullptr),
    m_background(nullptr)
{}

SlotControl::SlotControl(double x, double y, ShipSlotType slot_type) :
    GG::Control(GG::X0, GG::Y0, SLOT_CONTROL_WIDTH, SLOT_CONTROL_HEIGHT, GG::INTERACTIVE),
    m_highlighted(false),
    m_slot_type(slot_type),
    m_x_position_fraction(x),
    m_y_position_fraction(y),
    m_policy_control(nullptr),
    m_background(nullptr)
{}

void SlotControl::CompleteConstruction() {
    GG::Control::CompleteConstruction();

    m_background = GG::Wnd::Create<GG::StaticGraphic>(SlotBackgroundTexture(m_slot_type), GG::GRAPHIC_FITGRAPHIC | GG::GRAPHIC_PROPSCALE);
    m_background->Resize(GG::Pt(SLOT_CONTROL_WIDTH, SLOT_CONTROL_HEIGHT));
    m_background->Show();
    AttachChild(m_background);

    SetBrowseModeTime(GetOptionsDB().Get<int>("ui.tooltip.delay"));

    // set up empty slot tool tip
    std::string title_text;
    if (m_slot_type == SL_EXTERNAL)
        title_text = UserString("SL_EXTERNAL");
    else if (m_slot_type == SL_INTERNAL)
        title_text = UserString("SL_INTERNAL");
    else if (m_slot_type == SL_CORE)
        title_text = UserString("SL_CORE");

    SetBrowseInfoWnd(GG::Wnd::Create<IconTextBrowseWnd>(
        SlotBackgroundTexture(m_slot_type),
        title_text,
        UserString("SL_TOOLTIP_DESC")
    ));
}

bool SlotControl::EventFilter(GG::Wnd* w, const GG::WndEvent& event) {
    if (w == this)
        return false;

    switch (event.Type()) {
    case GG::WndEvent::DragDropEnter:
    case GG::WndEvent::DragDropHere:
    case GG::WndEvent::CheckDrops:
    case GG::WndEvent::DragDropLeave:
    case GG::WndEvent::DragDroppedOn:
        HandleEvent(event);
        return true;
        break;
    default:
        return false;
    }
}

void SlotControl::DropsAcceptable(DropsAcceptableIter first, DropsAcceptableIter last,
                                  const GG::Pt& pt, GG::Flags<GG::ModKey> mod_keys) const
{
    for (DropsAcceptableIter it = first; it != last; ++it)
        it->second = false;

    // if more than one control dropped somehow, reject all
    if (std::distance(first, last) != 1)
        return;

    for (DropsAcceptableIter it = first; it != last; ++it) {
        if (it->first->DragDropDataType() != POLICY_CONTROL_DROP_TYPE_STRING)
            continue;
        const auto policy_control = boost::polymorphic_downcast<const PolicyControl* const>(it->first);
        const Policy* policy_type = policy_control->Policy();
        if (policy_type &&
            policy_type->CanMountInSlotType(m_slot_type) &&
            policy_control != m_policy_control.get())
        {
            it->second = true;
            return;
        }
    }
}

ShipSlotType SlotControl::SlotType() const
{ return m_slot_type; }

double SlotControl::XPositionFraction() const
{ return m_x_position_fraction; }

double SlotControl::YPositionFraction() const
{ return m_y_position_fraction; }

const Policy* SlotControl::GetPolicy() const {
    if (m_policy_control)
        return m_policy_control->Policy();
    else
        return nullptr;
}

void SlotControl::StartingChildDragDrop(const GG::Wnd* wnd, const GG::Pt& offset) {
    if (!m_policy_control)
        return;

    const auto control = dynamic_cast<const PolicyControl*>(wnd);
    if (!control)
        return;

    if (control == m_policy_control.get())
        m_policy_control->Hide();
}

void SlotControl::CancellingChildDragDrop(const std::vector<const GG::Wnd*>& wnds) {
    if (!m_policy_control)
        return;

    for (const auto& wnd : wnds) {
        const auto control = dynamic_cast<const PolicyControl*>(wnd);
        if (!control)
            continue;

        if (control == m_policy_control.get())
            m_policy_control->Show();
    }
}

void SlotControl::AcceptDrops(const GG::Pt& pt, std::vector<std::shared_ptr<GG::Wnd>> wnds, GG::Flags<GG::ModKey> mod_keys) {
    if (wnds.size() != 1)
        ErrorLogger() << "SlotControl::AcceptDrops given multiple wnds unexpectedly...";

    const auto wnd = *(wnds.begin());
    const PolicyControl* control = boost::polymorphic_downcast<const PolicyControl*>(wnd.get());
    const Policy* policy_type = control ? control->Policy() : nullptr;

    if (policy_type)
        SlotContentsAlteredSignal(policy_type, (mod_keys & GG::MOD_KEY_CTRL));
}

void SlotControl::ChildrenDraggedAway(const std::vector<GG::Wnd*>& wnds, const GG::Wnd* destination) {
    if (wnds.empty())
        return;
    const GG::Wnd* wnd = wnds.front();
    const auto policy_control = dynamic_cast<const PolicyControl*>(wnd);
    if (policy_control != m_policy_control.get())
        return;
    DetachChildAndReset(m_policy_control);
    SlotContentsAlteredSignal(nullptr, false);
}

void SlotControl::DragDropEnter(const GG::Pt& pt, std::map<const Wnd*, bool>& drop_wnds_acceptable,
                                GG::Flags<GG::ModKey> mod_keys) {

    if (drop_wnds_acceptable.empty())
        return;

    DropsAcceptable(drop_wnds_acceptable.begin(), drop_wnds_acceptable.end(), pt, mod_keys);

    // Note:  If this SlotControl is being dragged over this indicates the dragged policy would
    //        replace this policy.
    if (drop_wnds_acceptable.begin()->second && m_policy_control)
        m_policy_control->Hide();
}

void SlotControl::DragDropLeave() {
    // Note:  If m_policy_control is being dragged, this does nothing, because it is detached.
    //        If this SlotControl is being dragged over this indicates the dragged policy would
    //        replace this policy.
    if (m_policy_control && !GG::GUI::GetGUI()->DragDropWnd(m_policy_control.get()))
        m_policy_control->Show();
}

void SlotControl::Render()
{}

void SlotControl::Highlight(bool actually)
{ m_highlighted = actually; }

void SlotControl::SetPolicy(const std::string& policy_name)
{ SetPolicy(GetPolicyType(policy_name)); }

void SlotControl::SetPolicy(const Policy* policy_type) {
    // remove existing policy control, if any
    DetachChildAndReset(m_policy_control);

    // create new policy control for passed in policy_type
    if (policy_type) {
        m_policy_control = GG::Wnd::Create<PolicyControl>(policy_type);
        AttachChild(m_policy_control);
        m_policy_control->InstallEventFilter(shared_from_this());

        // single click shows encyclopedia data
        m_policy_control->ClickedSignal.connect(
            PolicyTypeClickedSignal);

        // double click clears slot
        m_policy_control->DoubleClickedSignal.connect(
            [this](const Policy*){ this->SlotContentsAlteredSignal(nullptr, false); });
        SetBrowseModeTime(GetOptionsDB().Get<int>("ui.tooltip.delay"));

        // set policy occupying slot's tool tip to say slot type
        std::string title_text;
        if (m_slot_type == SL_EXTERNAL)
            title_text = UserString("SL_EXTERNAL");
        else if (m_slot_type == SL_INTERNAL)
            title_text = UserString("SL_INTERNAL");
        else if (m_slot_type == SL_CORE)
            title_text = UserString("SL_CORE");

        m_policy_control->SetBrowseInfoWnd(GG::Wnd::Create<IconTextBrowseWnd>(
            ClientUI::PolicyIcon(policy_type->Name()),
            UserString(policy_type->Name()) + " (" + title_text + ")",
            UserString(policy_type->Description())
        ));
    }
}

/** PolicysListBox accepts policys that are being removed from a SlotControl.*/
void PolicysListBox::DropsAcceptable(DropsAcceptableIter first, DropsAcceptableIter last,
                                   const GG::Pt& pt, GG::Flags<GG::ModKey> mod_keys) const
{
    for (DropsAcceptableIter it = first; it != last; ++it)
        it->second = false;

    // if more than one control dropped somehow, reject all
    if (std::distance(first, last) != 1)
        return;

    const auto&& parent = first->first->Parent();
    if (first->first->DragDropDataType() == POLICY_CONTROL_DROP_TYPE_STRING
        && parent
        && dynamic_cast<const SlotControl*>(parent.get()))
    {
        first->second = true;
    }
}

//////////////////////////////////////////////////
// DesignWnd::MainPanel                         //
//////////////////////////////////////////////////
class DesignWnd::MainPanel : public CUIWnd {
public:

    /** I18nString stores a string that might be in the stringtable. */
    class I18nString {
        public:
        I18nString (bool is_in_stringtable, const std::string& text) :
            m_is_in_stringtable(is_in_stringtable), m_text(text)
        {}
        I18nString (bool is_in_stringtable, std::string&& text) :
            m_is_in_stringtable(is_in_stringtable), m_text(std::move(text))
        {}

        /** Return the text a displayed. */
        std::string DisplayText() const
        { return m_is_in_stringtable ? UserString(m_text) : m_text; }

        /** Return the text as stored. */
        std::string StoredString() const
        { return m_text; }

        bool IsInStringtable() const
        { return m_is_in_stringtable; }

        private:
        const bool m_is_in_stringtable;
        const std::string m_text;
    };

    /** \name Structors */ //@{
    MainPanel(const std::string& config_name);
    void CompleteConstruction() override;
    //@}

    /** \name Accessors */ //@{
    /** If editing a current design return a ShipDesign* otherwise boost::none. */
    boost::optional<const ShipDesign*> EditingCurrentDesign() const;
    /** If editing a saved design return a ShipDesign* otherwise boost::none. */
    boost::optional<const ShipDesign*> EditingSavedDesign() const;

    const std::vector<std::string>      Policys() const;              //!< returns vector of names of policys in slots of current shown design.  empty slots are represented with empty stri
    const std::string&                  Hull() const;               //!< returns name of hull of current shown design
    bool                                IsDesignNameValid() const;  //!< checks design name validity
    /** Return a validated name and description.  If the design is a saved design then either both
        or neither will be stringtable values.*/
    std::pair<I18nString, I18nString>   ValidatedNameAndDescription() const;
    const I18nString                    ValidatedDesignName() const;//!< returns name currently entered for design or valid default
    const I18nString                    DesignDescription() const;  //!< returns description currently entered for design

    /** Returns a pointer to the design currently being modified (if any).  May
        return an empty pointer if not currently modifying a design. */
    std::shared_ptr<const ShipDesign>   GetIncompleteDesign() const;
    boost::optional<int>                GetReplacedDesignID() const;//!< returns ID of completed design selected to be replaced.

    /** If a design with the same hull and policys is registered with the empire then return the
        design name, otherwise return boost::none. */
    boost::optional<std::string>        CurrentDesignIsRegistered();
    //@}

    /** \name Mutators */ //@{
    void LClick(const GG::Pt& pt, GG::Flags<GG::ModKey> mod_keys) override;

    void AcceptDrops(const GG::Pt& pt, std::vector<std::shared_ptr<GG::Wnd>> wnds, GG::Flags<GG::ModKey> mod_keys) override;

    void SizeMove(const GG::Pt& ul, const GG::Pt& lr) override;

    void            Sanitize();

    void            SetPolicy(const std::string& policy_name, unsigned int slot);   //!< puts specified policy in specified slot.  does nothing if slot is out of range of available slots for current hull
    /** Sets the policy in \p slot to \p policy and emits and signal if requested.  Changes
        all similar policys if \p change_all_similar_policys. */
    void            SetPolicy(const Policy* policy, unsigned int slot, bool emit_signal = false, bool change_all_similar_policys = false);
    void            SetPolicys(const std::vector<std::string>& policys);            //!< puts specified policys in slots.  attempts to put each policy into the slot corresponding to its place in the passed vector.  if a policy cannot be placed, it is ignored.  more policys than there are slots available are ignored, and slots for which there are insufficient policys in the passed vector are unmodified

    /** Attempts to add the specified policy to the design, if possible.  will
      * first attempt to add policy to an empty slot of the appropriate type, and
      * if no appropriate slots are available, may or may not move other policys
      * around within the design to open up a compatible slot in which to add
      * this policy (and then add it).  may also do nothing. */
    void            AddPolicy(const Policy* policy);
    bool            CanPolicyBeAdded(const Policy* policy);

    void            ClearPolicys();                                               //!< removes all policys from design.  hull is not altered
    /** Remove policys called \p policy_name*/
    void            ClearPolicy(const std::string& policy_name);

    /** Set the design hull \p hull_name, displaying appropriate background image and creating
        appropriate SlotControls.  If \p signal is false do not emit the the
        DesignChangedSignal(). */
    void            SetHull(const std::string& hull_name, bool signal = true);
    void            SetHull(const HullType* hull, bool signal = true);
    void            SetDesign(const ShipDesign* ship_design);                   //!< sets the displayed design by setting the appropriate hull and policys
    void            SetDesign(int design_id);                                   //!< sets the displayed design by setting the appropriate hull and policys
    /** SetDesign to the design with \p uuid from the SavedDesignManager. */
    void            SetDesign(const boost::uuids::uuid& uuid);

    /** sets design hull and policys to those specified */
    void            SetDesignComponents(const std::string& hull,
                                        const std::vector<std::string>& policys);
    void            SetDesignComponents(const std::string& hull,
                                        const std::vector<std::string>& policys,
                                        const std::string& name,
                                        const std::string& desc);

    /** Add a design. */
    std::pair<int, boost::uuids::uuid>  AddDesign();
    /** Replace an existing design.*/
    void    ReplaceDesign();

    void            HighlightSlotType(std::vector<ShipSlotType>& slot_types);   //!< renders slots of the indicated types differently, perhaps to indicate that that those slots can be drop targets for a policyicular policy?

    /** Track changes in base type. */
    void            HandleBaseTypeChange(const DesignWnd::BaseSelector::BaseSelectorTab base_type);
    //@}

    /** emitted when the design is changed (by adding or removing policys, not
      * name or description changes) */
    mutable boost::signals2::signal<void ()>                DesignChangedSignal;

    /** emitted when the design name is changed */
    mutable boost::signals2::signal<void ()>                DesignNameChangedSignal;

    /** propagates signals from contained SlotControls that signal that a policy
      * has been clicked */
    mutable boost::signals2::signal<void (const Policy*, GG::Flags<GG::ModKey>)> PolicyTypeClickedSignal;

    mutable boost::signals2::signal<void (const HullType*)> HullTypeClickedSignal;

    /** emitted when the user clicks the m_replace_button to replace the currently selected
      * design with the new design in the player's empire */
    mutable boost::signals2::signal<void ()>                DesignReplacedSignal;

    /** emitted when the user clicks the m_confirm_button to add the new
      * design to the player's empire */
    mutable boost::signals2::signal<void ()>                DesignConfirmedSignal;

    /** emitted when the user clicks on the background of this main panel and
      * a completed design is showing */
    mutable boost::signals2::signal<void (int)>             CompleteDesignClickedSignal;

protected:
    void DropsAcceptable(DropsAcceptableIter first, DropsAcceptableIter last,
                         const GG::Pt& pt, GG::Flags<GG::ModKey> mod_keys) const override;

private:
    void            Populate();                         //!< creates and places SlotControls for current hull
    void            DoLayout();                         //!< positions buttons, text entry boxes and SlotControls
    void            DesignChanged();                    //!< responds to the design being changed
    void            DesignNameChanged();                //!< responds to the design name being changed
    void            RefreshIncompleteDesign() const;
    std::string     GetCleanDesignDump(const ShipDesign* ship_design);  //!< similar to ship design dump but without 'lookup_strings', icon and model entries

    bool            AddPolicyEmptySlot(const Policy* policy, int slot_number);                            //!< Adds policy to slot number
    bool            AddPolicyWithSwapping(const Policy* policy, std::pair<int, int> swap_and_empty_slot); //!< Swaps policy in slot # pair.first to slot # pair.second, adds given policy to slot # pair.first
    int             FindEmptySlotForPolicy(const Policy* policy);                                         //!< Determines if a policy can be added to any empty slot, returns the slot index if possible, otherwise -1

    void            DesignNameEditedSlot(const std::string& new_name);  //!< triggered when m_design_name's AfterTextChangedSignal fires. Used for basic name validation.

    std::pair<int, int> FindSlotForPolicyWithSwapping(const Policy* policy);                              //!< Determines if a policy can be added to a slot with swapping, returns a pair containing the slot to swap and an empty slot, otherwise a pair with -1
                                                                                                        //!< This function only tries to find a way to add the given policy by swapping a policy already in a slot to an empty slot
                                                                                                        //!< If theres an open slot that the given policy could go into but all of the occupied slots contain policys that can't swap into the open slot
                                                                                                        //!< This function will indicate that it could not add the policy, even though adding the policy is possible
    const HullType*                         m_hull;
    std::vector<std::shared_ptr<SlotControl>>               m_slots;

    // The design id if this design is replacable
    boost::optional<int>                    m_replaced_design_id;
    // The design uuid if this design is replacable
    boost::optional<boost::uuids::uuid>     m_replaced_design_uuid;

    /// Whether to add new designs to current or saved designs
    /// This tracks the last relevant selected tab in the base selector
    DesignWnd::BaseSelector::BaseSelectorTab m_type_to_create = DesignWnd::BaseSelector::BaseSelectorTab::Current;

    mutable std::shared_ptr<ShipDesign> m_incomplete_design;

    std::shared_ptr<GG::StaticGraphic>  m_background_image;
    std::shared_ptr<GG::Label>          m_design_name_label;
    std::shared_ptr<GG::Edit>           m_design_name;
    std::shared_ptr<GG::Label>          m_design_description_label;
    std::shared_ptr<GG::Edit>           m_design_description;
    std::shared_ptr<GG::Button>         m_replace_button;
    std::shared_ptr<GG::Button>         m_confirm_button;
    std::shared_ptr<GG::Button>         m_clear_button;
    bool                m_disabled_by_name; // if the design confirm button is currently disabled due to empty name
    bool                m_disabled_by_policy_conflict;

    boost::signals2::connection             m_empire_designs_changed_signal;
};


DesignWnd::MainPanel::MainPanel(const std::string& config_name) :
    CUIWnd(UserString("DESIGN_WND_MAIN_PANEL_TITLE"),
           GG::INTERACTIVE | GG::DRAGABLE | GG::RESIZABLE,
           config_name),
    m_hull(nullptr),
    m_slots(),
    m_replaced_design_id(boost::none),
    m_replaced_design_uuid(boost::none),
    m_incomplete_design(),
    m_background_image(nullptr),
    m_design_name_label(nullptr),
    m_design_name(nullptr),
    m_design_description_label(nullptr),
    m_design_description(nullptr),
    m_replace_button(nullptr),
    m_confirm_button(nullptr),
    m_clear_button(nullptr),
    m_disabled_by_name(false),
    m_disabled_by_policy_conflict(false)
{}

void DesignWnd::MainPanel::CompleteConstruction() {
    SetChildClippingMode(ClipToClient);

    m_design_name_label = GG::Wnd::Create<CUILabel>(UserString("DESIGN_WND_DESIGN_NAME"), GG::FORMAT_RIGHT, GG::INTERACTIVE);
    m_design_name = GG::Wnd::Create<CUIEdit>(UserString("DESIGN_NAME_DEFAULT"));
    m_design_description_label = GG::Wnd::Create<CUILabel>(UserString("DESIGN_WND_DESIGN_DESCRIPTION"), GG::FORMAT_RIGHT, GG::INTERACTIVE);
    m_design_description = GG::Wnd::Create<CUIEdit>(UserString("DESIGN_DESCRIPTION_DEFAULT"));
    m_replace_button = Wnd::Create<CUIButton>(UserString("DESIGN_WND_UPDATE"));
    m_confirm_button = Wnd::Create<CUIButton>(UserString("DESIGN_WND_ADD_FINISHED"));
    m_clear_button = Wnd::Create<CUIButton>(UserString("DESIGN_WND_CLEAR"));

    m_replace_button->SetBrowseModeTime(GetOptionsDB().Get<int>("ui.tooltip.delay"));
    m_confirm_button->SetBrowseModeTime(GetOptionsDB().Get<int>("ui.tooltip.delay"));

    AttachChild(m_design_name_label);
    AttachChild(m_design_name);
    AttachChild(m_design_description_label);
    AttachChild(m_design_description);
    AttachChild(m_replace_button);
    AttachChild(m_confirm_button);
    AttachChild(m_clear_button);

    m_clear_button->LeftClickedSignal.connect(
        boost::bind(&DesignWnd::MainPanel::ClearPolicys, this));
    m_design_name->EditedSignal.connect(
        boost::bind(&DesignWnd::MainPanel::DesignNameEditedSlot, this, _1));
    m_replace_button->LeftClickedSignal.connect(DesignReplacedSignal);
    m_confirm_button->LeftClickedSignal.connect(DesignConfirmedSignal);
    DesignChangedSignal.connect(boost::bind(&DesignWnd::MainPanel::DesignChanged, this));
    DesignReplacedSignal.connect(boost::bind(&DesignWnd::MainPanel::ReplaceDesign, this));
    DesignConfirmedSignal.connect(boost::bind(&DesignWnd::MainPanel::AddDesign, this));

    DesignChanged(); // Initialize components that rely on the current state of the design.

    CUIWnd::CompleteConstruction();

    DoLayout();
    SaveDefaultedOptions();
}

boost::optional<const ShipDesign*> DesignWnd::MainPanel::EditingSavedDesign() const {
    // Is there a valid replaced_uuid that indexes a saved design?
    if (!m_replaced_design_uuid)
        return boost::none;

    const auto maybe_design = GetSavedDesignsManager().GetDesign(*m_replaced_design_uuid);
    if (!maybe_design)
        return boost::none;
    return maybe_design;
}

boost::optional<const ShipDesign*> DesignWnd::MainPanel::EditingCurrentDesign() const {
    // Is there a valid replaced_uuid that indexes a saved design?
    if (!m_replaced_design_id || !GetCurrentDesignsManager().IsKnown(*m_replaced_design_id))
        return boost::none;

    const auto maybe_design = GetShipDesign(*m_replaced_design_id);
    if (!maybe_design)
        return boost::none;
    return maybe_design;
}

const std::vector<std::string> DesignWnd::MainPanel::Policys() const {
    std::vector<std::string> retval;
    for (const auto& slot : m_slots) {
        const Policy* policy_type = slot->GetPolicy();
        if (policy_type)
            retval.push_back(policy_type->Name());
        else
            retval.push_back("");
    }
    return retval;
}

const std::string& DesignWnd::MainPanel::Hull() const {
    if (m_hull)
        return m_hull->Name();
    else
        return EMPTY_STRING;
}

bool DesignWnd::MainPanel::IsDesignNameValid() const {
    // All whitespace probably shouldn't be OK either.
    return !m_design_name->Text().empty();
}


std::pair<DesignWnd::MainPanel::I18nString, DesignWnd::MainPanel::I18nString>
DesignWnd::MainPanel::ValidatedNameAndDescription() const
{
    const auto maybe_saved = EditingSavedDesign();

    // Determine if the title and descrition could both be string table values.

    // Is the title a stringtable index or the same as the saved designs value
    const std::string name_index =
        (UserStringExists(m_design_name->Text()) ? m_design_name->Text() :
         ((maybe_saved && (*maybe_saved)->LookupInStringtable()
           && (m_design_name->Text() == (*maybe_saved)->Name())) ? (*maybe_saved)->Name(false) : ""));

    // Is the descrition a stringtable index or the same as the saved designs value
    const std::string desc_index =
        (UserStringExists(m_design_description->Text()) ? m_design_description->Text() :
         ((maybe_saved && (*maybe_saved)->LookupInStringtable()
           && (m_design_description->Text() == (*maybe_saved)->Description())) ? (*maybe_saved)->Description(false) : ""));

    // Are both the title and the description string table lookup values
    if (!name_index.empty() && !desc_index.empty())
        return std::make_pair(
            I18nString(true, name_index),
            I18nString(true, desc_index));

    return std::make_pair(
        I18nString(false, (IsDesignNameValid()) ? m_design_name->Text() : UserString("DESIGN_NAME_DEFAULT")),
        I18nString(false, m_design_description->Text()));
}

const DesignWnd::MainPanel::I18nString DesignWnd::MainPanel::ValidatedDesignName() const
{ return ValidatedNameAndDescription().first; }

const DesignWnd::MainPanel::I18nString DesignWnd::MainPanel::DesignDescription() const
{ return ValidatedNameAndDescription().second; }

std::shared_ptr<const ShipDesign> DesignWnd::MainPanel::GetIncompleteDesign() const {
    RefreshIncompleteDesign();
    return m_incomplete_design;
}

boost::optional<int> DesignWnd::MainPanel::GetReplacedDesignID() const
{ return m_replaced_design_id; }

boost::optional<std::string> DesignWnd::MainPanel::CurrentDesignIsRegistered() {
    int empire_id = HumanClientApp::GetApp()->EmpireID();
    const auto empire = GetEmpire(empire_id);
    if (!empire) {
        ErrorLogger() << "DesignWnd::MainPanel::CurrentDesignIsRegistered couldn't get the current empire.";
        return boost::none;
    }

    if (const auto& cur_design = GetIncompleteDesign()) {
        for (const auto design_id : empire->ShipDesigns()) {
            const auto& ship_design = *GetShipDesign(design_id);
            if (ship_design == *cur_design.get())
                return ship_design.Name();
        }
    }
    return boost::none;
}

void DesignWnd::MainPanel::LClick(const GG::Pt& pt, GG::Flags<GG::ModKey> mod_keys) {
    if (m_hull)
        HullTypeClickedSignal(m_hull);
    CUIWnd::LClick(pt, mod_keys);
}

void DesignWnd::MainPanel::SizeMove(const GG::Pt& ul, const GG::Pt& lr) {
    CUIWnd::SizeMove(ul, lr);
    DoLayout();
}

void DesignWnd::MainPanel::Sanitize() {
    SetHull(nullptr);
    m_design_name->SetText(UserString("DESIGN_NAME_DEFAULT"));
    m_design_description->SetText(UserString("DESIGN_DESCRIPTION_DEFAULT"));
    // disconnect old empire design signal
    m_empire_designs_changed_signal.disconnect();
}

void DesignWnd::MainPanel::SetPolicy(const std::string& policy_name, unsigned int slot)
{ SetPolicy(GetPolicyType(policy_name), slot); }

void DesignWnd::MainPanel::SetPolicy(const Policy* policy, unsigned int slot, bool emit_signal /* = false */, bool change_all_similar_policys /*= false*/) {
    //DebugLogger() << "DesignWnd::MainPanel::SetPolicy(" << (policy ? policy->Name() : "no policy") << ", slot " << slot << ")";
    if (slot > m_slots.size()) {
        ErrorLogger() << "DesignWnd::MainPanel::SetPolicy specified nonexistant slot";
        return;
    }

    if (!change_all_similar_policys) {
        m_slots[slot]->SetPolicy(policy);

    } else {
        const auto original_policy = m_slots[slot]->GetPolicy();
        std::string original_policy_name = original_policy ? original_policy->Name() : "";

        if (change_all_similar_policys) {
            for (auto& slot : m_slots) {
                // skip incompatible slots
                if (!policy->CanMountInSlotType(slot->SlotType()))
                    continue;

                // skip different type policys
                const auto replaced_policy = slot->GetPolicy();
                if (replaced_policy && (replaced_policy->Name() != original_policy_name))
                    continue;

                slot->SetPolicy(policy);
            }
        }
    }

    if (emit_signal)  // to avoid unnecessary signal repetition.
        DesignChangedSignal();
}

void DesignWnd::MainPanel::SetPolicys(const std::vector<std::string>& policys) {
    unsigned int num_policys = std::min(policys.size(), m_slots.size());
    for (unsigned int i = 0; i < num_policys; ++i)
        m_slots[i]->SetPolicy(policys[i]);

    DesignChangedSignal();
}

void DesignWnd::MainPanel::AddPolicy(const Policy* policy) {
    if (AddPolicyEmptySlot(policy, FindEmptySlotForPolicy(policy)))
        return;

    if (!AddPolicyWithSwapping(policy, FindSlotForPolicyWithSwapping(policy)))
        DebugLogger() << "DesignWnd::MainPanel::AddPolicy(" << (policy ? policy->Name() : "no policy")
                      << ") couldn't find a slot for the policy";
}

bool DesignWnd::MainPanel::CanPolicyBeAdded(const Policy* policy) {
    std::pair<int, int> swap_result = FindSlotForPolicyWithSwapping(policy);
    return (FindEmptySlotForPolicy(policy) >= 0 || (swap_result.first >= 0 && swap_result.second >= 0));
}

bool DesignWnd::MainPanel::AddPolicyEmptySlot(const Policy* policy, int slot_number) {
    if (!policy || slot_number < 0)
        return false;
    SetPolicy(policy, slot_number);
    DesignChangedSignal();
    return true;
}

bool DesignWnd::MainPanel::AddPolicyWithSwapping(const Policy* policy, std::pair<int, int> swap_and_empty_slot) {
    if (!policy || swap_and_empty_slot.first < 0 || swap_and_empty_slot.second < 0)
        return false;
    // Move the flexible policy to the first open spot
    SetPolicy(m_slots[swap_and_empty_slot.first]->GetPolicy(), swap_and_empty_slot.second);
    // Move replacement policy into the newly opened slot
    SetPolicy(policy, swap_and_empty_slot.first);
    DesignChangedSignal();
    return true;
}

int DesignWnd::MainPanel::FindEmptySlotForPolicy(const Policy* policy) {
    int result = -1;
    if (!policy)
        return result;

    if (policy->Class() == PC_FIGHTER_HANGAR) {
        // give up if policy is a hangar and there is already a hangar of another type
        std::string already_seen_hangar_name;
        for (const auto& slot : m_slots) {
            const Policy* policy_type = slot->GetPolicy();
            if (!policy_type || policy_type->Class() != PC_FIGHTER_HANGAR)
                continue;
            if (policy_type->Name() != policy->Name())
                return result;
        }
    }

    for (unsigned int i = 0; i < m_slots.size(); ++i) {             // scan through slots to find one that can mount policy
        const ShipSlotType slot_type = m_slots[i]->SlotType();
        const Policy* policy_type = m_slots[i]->GetPolicy();          // check if this slot is empty

        if (!policy_type && policy->CanMountInSlotType(slot_type)) {    // ... and if the policy can mount here
            result = i;
            return result;
        }
    }
    return result;
}

void DesignWnd::MainPanel::DesignNameEditedSlot(const std::string& new_name) {
    DesignNameChanged();  // Check whether the confirmation button should be enabled or disabled each time the name changes.
}

std::pair<int, int> DesignWnd::MainPanel::FindSlotForPolicyWithSwapping(const Policy* policy) {
    // result.first = swap_slot, result.second = empty_slot
    // if any of the pair == -1, no swap!

    if (!policy)
        return {-1, -1};

    // check if adding the policy would cause the design to have multiple different types of hangar (which is not allowed)
    if (policy->Class() == PC_FIGHTER_HANGAR) {
        for (const auto& slot : m_slots) {
            const Policy* existing_policy = slot->GetPolicy();
            if (!existing_policy || existing_policy->Class() != PC_FIGHTER_HANGAR)
                continue;
            if (existing_policy->Name() != policy->Name())
                return {-1, -1};  // conflict; new policy can't be added
        }
    }

    // first search for an empty compatible slot for the new policy
    for (const auto& slot : m_slots) {
        if (!policy->CanMountInSlotType(slot->SlotType()))
            continue;   // skip incompatible slots

        if (!slot->GetPolicy())
            return {-1, -1};  // empty slot that can hold policy. no swapping needed.
    }


    // second, scan for a slot containing a policy that can be moved to another
    // slot to make room for the new policy
    for (unsigned int i = 0; i < m_slots.size(); ++i) {
        if (!policy->CanMountInSlotType(m_slots[i]->SlotType()))
            continue;   // skip incompatible slots

        // can now assume m_slots[i] has a policy, as if it didn't, it would have
        // been found in the first loop

        // see if we can move the policy in the candidate slot to an empty slot elsewhere
        for (unsigned int j = 0; j < m_slots.size(); ++j) {
            if (m_slots[j]->GetPolicy())
                continue;   // only consider moving into empty slots

            if (m_slots[i]->GetPolicy()->CanMountInSlotType(m_slots[j]->SlotType()))
                return {i, j};    // other slot can hold current policy to make room for new policy
        }
    }

    return {-1, -1};
}

void DesignWnd::MainPanel::ClearPolicys() {
    for (auto& slot : m_slots)
        slot->SetPolicy(nullptr);
    DesignChangedSignal();
}

void DesignWnd::MainPanel::ClearPolicy(const std::string& policy_name) {
    bool changed = false;
    for (const auto& slot : m_slots) {
        const Policy* existing_policy = slot->GetPolicy();
        if (!existing_policy)
            continue;
        if (existing_policy->Name() != policy_name)
            continue;
        slot->SetPolicy(nullptr);
        changed = true;
    }

    if (changed)
        DesignChangedSignal();
}

void DesignWnd::MainPanel::SetHull(const std::string& hull_name, bool signal)
{ SetHull(GetHullType(hull_name), signal); }

void DesignWnd::MainPanel::SetHull(const HullType* hull, bool signal) {
    m_hull = hull;
    DetachChild(m_background_image);
    m_background_image = nullptr;
    if (m_hull) {
        std::shared_ptr<GG::Texture> texture = ClientUI::HullTexture(hull->Name());
        m_background_image = GG::Wnd::Create<GG::StaticGraphic>(texture, GG::GRAPHIC_PROPSCALE | GG::GRAPHIC_FITGRAPHIC);
        AttachChild(m_background_image);
        MoveChildDown(m_background_image);
    }
    Populate();
    DoLayout();
    if (signal)
        DesignChangedSignal();
}

void DesignWnd::MainPanel::SetDesign(const ShipDesign* ship_design) {
    m_incomplete_design.reset();

    if (!ship_design) {
        SetHull(nullptr);
        return;
    }

    if (!ship_design->IsMonster()) {
        m_replaced_design_id = ship_design->ID();
        m_replaced_design_uuid = ship_design->UUID();
    } else {
        // Allow editing of monsters if the design is a saved design
        const auto is_saved_monster = GetSavedDesignsManager().GetDesign(ship_design->UUID());
        m_replaced_design_id = is_saved_monster ? ship_design->ID() : boost::optional<int>();
        m_replaced_design_uuid = is_saved_monster ? ship_design->UUID() : boost::optional<boost::uuids::uuid>();
    }

    m_design_name->SetText(ship_design->Name());
    m_design_description->SetText(ship_design->Description());

    bool suppress_design_changed_signal = true;
    SetHull(ship_design->GetHull(), !suppress_design_changed_signal);

    SetPolicys(ship_design->Policys());
    DesignChangedSignal();
}

void DesignWnd::MainPanel::SetDesign(int design_id)
{ SetDesign(GetShipDesign(design_id)); }

void DesignWnd::MainPanel::SetDesign(const boost::uuids::uuid& uuid)
{ SetDesign(GetSavedDesignsManager().GetDesign(uuid)); }

void DesignWnd::MainPanel::SetDesignComponents(const std::string& hull,
                                               const std::vector<std::string>& policys)
{
    m_replaced_design_id = boost::none;
    m_replaced_design_uuid = boost::none;
    SetHull(hull, false);
    SetPolicys(policys);
}

void DesignWnd::MainPanel::SetDesignComponents(const std::string& hull,
                                               const std::vector<std::string>& policys,
                                               const std::string& name,
                                               const std::string& desc)
{
    SetDesignComponents(hull, policys);
    m_design_name->SetText(name);
    m_design_description->SetText(desc);
}

void DesignWnd::MainPanel::HighlightSlotType(std::vector<ShipSlotType>& slot_types) {
    for (auto& control : m_slots) {
        ShipSlotType slot_type = control->SlotType();
        if (std::find(slot_types.begin(), slot_types.end(), slot_type) != slot_types.end())
            control->Highlight(true);
        else
            control->Highlight(false);
    }
}

void DesignWnd::MainPanel::HandleBaseTypeChange(DesignWnd::BaseSelector::BaseSelectorTab base_type) {
    if (m_type_to_create == base_type)
        return;
    switch (base_type) {
    case DesignWnd::BaseSelector::BaseSelectorTab::Current:
    case DesignWnd::BaseSelector::BaseSelectorTab::Saved:
        m_type_to_create = base_type;
        break;
    default:
        break;
    }
    DesignChanged();
}

void DesignWnd::MainPanel::Populate(){
    for (const auto& slot: m_slots)
        DetachChild(slot);
    m_slots.clear();

    if (!m_hull)
        return;

    const std::vector<HullType::Slot>& hull_slots = m_hull->Slots();

    for (std::vector<HullType::Slot>::size_type i = 0; i != hull_slots.size(); ++i) {
        const HullType::Slot& slot = hull_slots[i];
        auto slot_control = GG::Wnd::Create<SlotControl>(slot.x, slot.y, slot.type);
        m_slots.push_back(slot_control);
        AttachChild(slot_control);
        slot_control->SlotContentsAlteredSignal.connect(
            boost::bind(static_cast<void (DesignWnd::MainPanel::*)(const Policy*, unsigned int, bool, bool)>(&DesignWnd::MainPanel::SetPolicy), this, _1, i, true, _2));
        slot_control->PolicyTypeClickedSignal.connect(
            PolicyTypeClickedSignal);
    }
}

void DesignWnd::MainPanel::DoLayout() {
    // position labels and text edit boxes for name and description and buttons to clear and confirm design

    const int PTS = ClientUI::Pts();
    const GG::X PTS_WIDE(PTS / 2);           // guess at how wide per character the font needs
    const GG::Y BUTTON_HEIGHT(PTS * 2);
    const GG::X LABEL_WIDTH = PTS_WIDE * 15;
    const int PAD = 6;
    const int GUESSTIMATE_NUM_CHARS_IN_BUTTON_TEXT = 25;    // rough guesstimate... avoid overly long policy class names
    const GG::X BUTTON_WIDTH = PTS_WIDE*GUESSTIMATE_NUM_CHARS_IN_BUTTON_TEXT;

    GG::X edit_right = ClientWidth();
    GG::X confirm_right = ClientWidth() - PAD;

    GG::Pt lr = GG::Pt(confirm_right, BUTTON_HEIGHT) + GG::Pt(GG::X0, GG::Y(PAD));
    GG::Pt ul = lr - GG::Pt(BUTTON_WIDTH, BUTTON_HEIGHT);
    m_confirm_button->SizeMove(ul, lr);

    lr = lr - GG::Pt(BUTTON_WIDTH, GG::Y(0))- GG::Pt(GG::X(PAD),GG::Y(0));
    ul = lr - GG::Pt(BUTTON_WIDTH, BUTTON_HEIGHT);
    m_replace_button->SizeMove(ul, lr);

    edit_right = ul.x - PAD;

    lr = ClientSize() + GG::Pt(-GG::X(PAD), -GG::Y(PAD));
    ul = lr - GG::Pt(BUTTON_WIDTH, BUTTON_HEIGHT);
    m_clear_button->SizeMove(ul, lr);

    ul = GG::Pt(GG::X(PAD), GG::Y(PAD));
    lr = ul + GG::Pt(LABEL_WIDTH, m_design_name->MinUsableSize().y);
    m_design_name_label->SizeMove(ul, lr);

    ul.x += lr.x;
    lr.x = edit_right;
    m_design_name->SizeMove(ul, lr);

    ul.x = GG::X(PAD);
    ul.y += (m_design_name->Height() + PAD);
    lr = ul + GG::Pt(LABEL_WIDTH, m_design_name->MinUsableSize().y);
    m_design_description_label->SizeMove(ul, lr);

    ul.x = lr.x + PAD;
    lr.x = confirm_right;
    m_design_description->SizeMove(ul, lr);

    // place background image of hull
    ul.x = GG::X0;
    ul.y += m_design_name->Height();
    GG::Rect background_rect = GG::Rect(ul, ClientLowerRight());

    if (m_background_image) {
        GG::Pt bg_ul = background_rect.UpperLeft();
        GG::Pt bg_lr = ClientSize();
        m_background_image->SizeMove(bg_ul, bg_lr);
        background_rect = m_background_image->RenderedArea();
    }

    // place slot controls over image of hull
    for (auto& slot : m_slots) {
        GG::X x(background_rect.Left() - slot->Width()/2 - ClientUpperLeft().x + slot->XPositionFraction() * background_rect.Width());
        GG::Y y(background_rect.Top() - slot->Height()/2 - ClientUpperLeft().y + slot->YPositionFraction() * background_rect.Height());
        slot->MoveTo(GG::Pt(x, y));
    }
}

void DesignWnd::MainPanel::DesignChanged() {
    m_replace_button->ClearBrowseInfoWnd();
    m_confirm_button->ClearBrowseInfoWnd();

    int client_empire_id = HumanClientApp::GetApp()->EmpireID();
    m_disabled_by_name = false;
    m_disabled_by_policy_conflict = false;

    m_replace_button->Disable(true);
    m_confirm_button->Disable(true);

    m_replace_button->SetText(UserString("DESIGN_WND_UPDATE_FINISHED"));
    m_confirm_button->SetText(UserString("DESIGN_WND_ADD_FINISHED"));

    if (!m_hull) {
        m_replace_button->SetBrowseInfoWnd(GG::Wnd::Create<TextBrowseWnd>(
            UserString("DESIGN_INVALID"), UserString("DESIGN_UPDATE_INVALID_NO_CANDIDATE")));
        m_confirm_button->SetBrowseInfoWnd(GG::Wnd::Create<TextBrowseWnd>(
            UserString("DESIGN_INVALID"), UserString("DESIGN_INV_NO_HULL")));
        return;
    }

    if (client_empire_id == ALL_EMPIRES) {
        m_replace_button->SetBrowseInfoWnd(GG::Wnd::Create<TextBrowseWnd>(
            UserString("DESIGN_INVALID"), UserString("DESIGN_INV_MODERATOR")));
        m_confirm_button->SetBrowseInfoWnd(GG::Wnd::Create<TextBrowseWnd>(
            UserString("DESIGN_INVALID"), UserString("DESIGN_INV_MODERATOR")));
        return;
    }

    if (!IsDesignNameValid()) {
        m_disabled_by_name = true;

        m_replace_button->SetBrowseInfoWnd(GG::Wnd::Create<TextBrowseWnd>(
            UserString("DESIGN_INVALID"), UserString("DESIGN_INV_NO_NAME")));
        m_confirm_button->SetBrowseInfoWnd(GG::Wnd::Create<TextBrowseWnd>(
            UserString("DESIGN_INVALID"), UserString("DESIGN_INV_NO_NAME")));
        return;
    }

    if (!ShipDesign::ValidDesign(m_hull->Name(), Policys())) {
        // if a design has exclusion violations between policys and hull, highlight these and indicate it on the button

        std::pair<std::string, std::string> problematic_components;

        // check hull exclusions against all policys...
        const std::set<std::string>& hull_exclusions = m_hull->Exclusions();
        for (const std::string& policy_name : Policys()) {
            if (policy_name.empty())
                continue;
            if (hull_exclusions.find(policy_name) != hull_exclusions.end()) {
                m_disabled_by_policy_conflict = true;
                problematic_components.first = m_hull->Name();
                problematic_components.second = policy_name;
            }
        }

        // check policy exclusions against other policys and hull
        std::set<std::string> already_seen_component_names;
        already_seen_component_names.insert(m_hull->Name());
        for (const std::string& policy_name : Policys()) {
            if (m_disabled_by_policy_conflict)
                break;
            const Policy* policy_type = GetPolicyType(policy_name);
            if (!policy_type)
                continue;
            for (const std::string& excluded_policy : policy_type->Exclusions()) {
                if (already_seen_component_names.find(excluded_policy) != already_seen_component_names.end()) {
                    m_disabled_by_policy_conflict = true;
                    problematic_components.first = policy_name;
                    problematic_components.second = excluded_policy;
                    break;
                }
            }
            already_seen_component_names.insert(policy_name);
        }


        if (m_disabled_by_policy_conflict) {
            m_replace_button->SetBrowseInfoWnd(GG::Wnd::Create<TextBrowseWnd>(
                UserString("DESIGN_WND_COMPONENT_CONFLICT"),
                boost::io::str(FlexibleFormat(UserString("DESIGN_WND_COMPONENT_CONFLICT_DETAIL"))
                               % UserString(problematic_components.first)
                               % UserString(problematic_components.second))));
            m_confirm_button->SetBrowseInfoWnd(GG::Wnd::Create<TextBrowseWnd>(
                UserString("DESIGN_WND_COMPONENT_CONFLICT"),
                boost::io::str(FlexibleFormat(UserString("DESIGN_WND_COMPONENT_CONFLICT_DETAIL"))
                               % UserString(problematic_components.first)
                               % UserString(problematic_components.second))));

            // todo: mark conflicting policys somehow
        }
        return;
    }

    const auto& cur_design = GetIncompleteDesign();

    if (!cur_design)
        return;

    const auto new_design_name = ValidatedDesignName().DisplayText();

    // producible only matters for empire designs.
    // Monster designs can be edited as saved designs.
    bool producible = cur_design->Producible();

    // Current designs can not duplicate other designs, be already registered.
    const auto existing_design_name = CurrentDesignIsRegistered();

    const auto& replaced_saved_design = EditingSavedDesign();

    const auto& replaced_current_design = EditingCurrentDesign();

    // Choose text for the replace button: replace saved design, replace current design or already known.

    // A changed saved design can be replaced with an updated design
    if (replaced_saved_design) {
        if (cur_design && !(*cur_design == **replaced_saved_design)) {
            m_replace_button->SetText(UserString("DESIGN_WND_UPDATE_SAVED"));
            m_replace_button->SetBrowseInfoWnd(
                GG::Wnd::Create<TextBrowseWnd>(
                    UserString("DESIGN_WND_UPDATE_SAVED"),
                    boost::io::str(FlexibleFormat(UserString("DESIGN_WND_UPDATE_SAVED_DETAIL"))
                                   % (*replaced_saved_design)->Name()
                                   % new_design_name)));
            m_replace_button->Disable(false);
        }
    }

    if (producible && replaced_current_design) {
        if (!existing_design_name) {
            // A current design can be replaced if it doesn't duplicate an existing design
            m_replace_button->SetBrowseInfoWnd(GG::Wnd::Create<TextBrowseWnd>(
                UserString("DESIGN_WND_UPDATE_FINISHED"),
                boost::io::str(FlexibleFormat(UserString("DESIGN_WND_UPDATE_FINISHED_DETAIL"))
                               % (*replaced_current_design)->Name()
                               % new_design_name)));
            m_replace_button->Disable(false);
        } else {
            // Otherwise mark it as known.
            m_replace_button->SetBrowseInfoWnd(GG::Wnd::Create<TextBrowseWnd>(
                UserString("DESIGN_WND_KNOWN"),
                boost::io::str(FlexibleFormat(UserString("DESIGN_WND_KNOWN_DETAIL"))
                               % *existing_design_name)));
        }
    }

    // Choose text for the add new design button: add saved design, add current design or already known.

    // Add a saved design if the saved base selector was visited more recently than the current tab.
    if (m_type_to_create == DesignWnd::BaseSelector::BaseSelectorTab::Saved) {
        // A new saved design can always be created
        m_confirm_button->SetText(UserString("DESIGN_WND_ADD_SAVED"));
        m_confirm_button->SetBrowseInfoWnd(
            GG::Wnd::Create<TextBrowseWnd>(
                UserString("DESIGN_WND_ADD_SAVED"),
                boost::io::str(FlexibleFormat(UserString("DESIGN_WND_ADD_SAVED_DETAIL"))
                               % new_design_name)));
        m_confirm_button->Disable(false);
    } else if (producible) {
        if (!existing_design_name) {
            // A new current can be added if it does not duplicate an existing design.
            m_confirm_button->SetBrowseInfoWnd(GG::Wnd::Create<TextBrowseWnd>(
                UserString("DESIGN_WND_ADD_FINISHED"),
                boost::io::str(FlexibleFormat(UserString("DESIGN_WND_ADD_FINISHED_DETAIL"))
                               % new_design_name)));
            m_confirm_button->Disable(false);

        } else {
            // Otherwise the design is already known.
            m_confirm_button->SetBrowseInfoWnd(GG::Wnd::Create<TextBrowseWnd>(
                UserString("DESIGN_WND_KNOWN"),
                boost::io::str(FlexibleFormat(UserString("DESIGN_WND_KNOWN_DETAIL"))
                               % *existing_design_name)));
        }
    }
}

void DesignWnd::MainPanel::DesignNameChanged() {
    if (m_disabled_by_name || (!IsDesignNameValid() && !m_confirm_button->Disabled()))
        DesignChangedSignal();
    else if (GetOptionsDB().Get<bool>("ui.design.pedia.title.dynamic.enabled"))
        DesignNameChangedSignal();
    else
        RefreshIncompleteDesign();
}

std::string DesignWnd::MainPanel::GetCleanDesignDump(const ShipDesign* ship_design) {
    std::string retval = "ShipDesign\n";
    retval += ship_design->Name() + "\"\n";
    retval += ship_design->Hull() + "\"\n";
    for (const std::string& policy_name : ship_design->Policys()) {
        retval += "\"" + policy_name + "\"\n";
    }
    return retval;
}

void DesignWnd::MainPanel::RefreshIncompleteDesign() const {
    const auto name_and_description = ValidatedNameAndDescription();
    const auto& name = name_and_description.first;
    const auto& description = name_and_description.second;

    if (ShipDesign* design = m_incomplete_design.get()) {
        if (design->Hull() ==             Hull() &&
            design->Name(false) ==        name.StoredString() &&
            design->Description(false) == description.StoredString() &&
            design->Policys() ==            Policys())
        {
            // nothing has changed, so don't need to update
            return;
        }
    }

    // assemble and check info for new design
    const std::string& hull =           Hull();
    std::vector<std::string> policys =    Policys();

    const std::string& icon = m_hull ? m_hull->Icon() : EMPTY_STRING;

    const auto uuid = boost::uuids::random_generator()();

    // update stored design
    m_incomplete_design.reset();
    if (hull.empty())
        return;
    try {
        m_incomplete_design = std::make_shared<ShipDesign>(
            std::invalid_argument(""),
            name.StoredString(), description.StoredString(),
            CurrentTurn(), ClientApp::GetApp()->EmpireID(),
            hull, policys, icon, "", name.IsInStringtable(),
            false, uuid);
    } catch (const std::invalid_argument& e) {
        ErrorLogger() << "DesignWnd::MainPanel::RefreshIncompleteDesign " << e.what();
    }
}

void DesignWnd::MainPanel::DropsAcceptable(DropsAcceptableIter first, DropsAcceptableIter last,
                                           const GG::Pt& pt, GG::Flags<GG::ModKey> mod_keys) const
{
    for (DropsAcceptableIter it = first; it != last; ++it)
        it->second = false;

    // if multiple things dropped simultaneously somehow, reject all
    if (std::distance(first, last) != 1)
        return;

    if (dynamic_cast<const BasesListBox::BasesListBoxRow*>(first->first))
        first->second = true;
}

void DesignWnd::MainPanel::AcceptDrops(const GG::Pt& pt, std::vector<std::shared_ptr<GG::Wnd>> wnds, GG::Flags<GG::ModKey> mod_keys) {
    if (wnds.size() != 1)
        ErrorLogger() << "DesignWnd::MainPanel::AcceptDrops given multiple wnds unexpectedly...";

    const auto& wnd = *(wnds.begin());
    if (!wnd)
        return;

    if (const auto completed_design_row = dynamic_cast<const BasesListBox::CompletedDesignListBoxRow*>(wnd.get())) {
        SetDesign(GetShipDesign(completed_design_row->DesignID()));
    }
    else if (const auto hullandpolicys_row = dynamic_cast<const BasesListBox::HullAndPolicysListBoxRow*>(wnd.get())) {
        const std::string& hull = hullandpolicys_row->Hull();
        const std::vector<std::string>& policys = hullandpolicys_row->Policys();

        SetDesignComponents(hull, policys);
    }
    else if (const auto saved_design_row = dynamic_cast<const SavedDesignsListBox::SavedDesignListBoxRow*>(wnd.get())) {
        const auto& uuid = saved_design_row->DesignUUID();
        SetDesign(GetSavedDesignsManager().GetDesign(uuid));
    }
}

std::pair<int, boost::uuids::uuid> DesignWnd::MainPanel::AddDesign() {
    try {
        std::vector<std::string> policys = Policys();
        const std::string& hull_name = Hull();

        const auto name = ValidatedDesignName();

        const auto description = DesignDescription();

        std::string icon = "ship_hulls/generic_hull.png";
        if (const HullType* hull = GetHullType(hull_name))
            icon = hull->Icon();

        auto new_uuid = boost::uuids::random_generator()();
        auto new_design_id = INVALID_DESIGN_ID;

        // create design from stuff chosen in UI
        ShipDesign design(std::invalid_argument(""),
                          name.StoredString(), description.StoredString(),
                          CurrentTurn(), ClientApp::GetApp()->EmpireID(),
                          hull_name, policys, icon, "some model", name.IsInStringtable(),
                          false, new_uuid);

        // If editing a saved design insert into saved designs
        if (m_type_to_create == DesignWnd::BaseSelector::BaseSelectorTab::Saved) {
            auto& manager = GetSavedDesignsManager();
            manager.InsertBefore(design, manager.OrderedDesignUUIDs().begin());
            new_uuid = *manager.OrderedDesignUUIDs().begin();

        // Otherwise insert into current empire designs
        } else {
            int empire_id = HumanClientApp::GetApp()->EmpireID();
            const Empire* empire = GetEmpire(empire_id);
            if (!empire) return {INVALID_DESIGN_ID, boost::uuids::uuid{{0}}};

            auto order = std::make_shared<ShipDesignOrder>(empire_id, design);
            HumanClientApp::GetApp()->Orders().IssueOrder(order);
            new_design_id = order->DesignID();

            auto& manager = GetCurrentDesignsManager();
            const auto& all_ids = manager.AllOrderedIDs();
            manager.InsertBefore(new_design_id, all_ids.empty() ? INVALID_DESIGN_ID : *all_ids.begin());
        }

        DesignChangedSignal();

        DebugLogger() << "Added new design: " << design.Name();

        return std::make_pair(new_design_id, new_uuid);

    } catch (std::invalid_argument&) {
        ErrorLogger() << "DesignWnd::AddDesign tried to add an invalid ShipDesign";
        return {INVALID_DESIGN_ID, boost::uuids::uuid{{0}}};
    }
}

void DesignWnd::MainPanel::ReplaceDesign() {
    auto old_m_type_to_create = m_type_to_create;
    m_type_to_create = EditingSavedDesign()
        ? DesignWnd::BaseSelector::BaseSelectorTab::Saved
        : DesignWnd::BaseSelector::BaseSelectorTab::Current;

    const auto new_id_and_uuid = AddDesign();
    const auto& new_uuid = new_id_and_uuid.second;
    const auto new_design_id = new_id_and_uuid.first;

    m_type_to_create = old_m_type_to_create;

    // If replacing a saved design
    if (const auto replaced_design = EditingSavedDesign()) {
        auto& manager = GetSavedDesignsManager();

        manager.MoveBefore(new_uuid, (*replaced_design)->UUID());
        manager.Erase((*replaced_design)->UUID());

        // Update the replaced design on the bench
        SetDesign(manager.GetDesign(new_uuid));

    } else if (const auto current_maybe_design = EditingCurrentDesign()) {
        auto& manager = GetCurrentDesignsManager();
        int empire_id = HumanClientApp::GetApp()->EmpireID();
        int replaced_id = (*current_maybe_design)->ID();

        if (new_design_id == INVALID_DESIGN_ID) return;

        // Remove the old id from the Empire.
        const auto maybe_obsolete = manager.IsObsolete(replaced_id);
        bool is_obsolete = maybe_obsolete && *maybe_obsolete;
        if (!is_obsolete)
            HumanClientApp::GetApp()->Orders().IssueOrder(
                std::make_shared<ShipDesignOrder>(empire_id, replaced_id, true));

        // Replace the old id in the manager.
        manager.MoveBefore(new_design_id, replaced_id);
        manager.Remove(replaced_id);

        // Update the replaced design on the bench
        SetDesign(new_design_id);

        DebugLogger() << "Replaced design #" << replaced_id << " with #" << new_design_id ;
    }

    DesignChangedSignal();
}


//////////////////////////////////////////////////
// DesignWnd                                    //
//////////////////////////////////////////////////
DesignWnd::DesignWnd(GG::X w, GG::Y h) :
    GG::Wnd(GG::X0, GG::Y0, w, h, GG::ONTOP | GG::INTERACTIVE),
    m_detail_panel(nullptr),
    m_base_selector(nullptr),
    m_policy_palette(nullptr),
    m_main_panel(nullptr)
{}

void DesignWnd::CompleteConstruction() {
    GG::Wnd::CompleteConstruction();

    Sound::TempUISoundDisabler sound_disabler;
    SetChildClippingMode(ClipToClient);

    m_detail_panel = GG::Wnd::Create<EncyclopediaDetailPanel>(GG::ONTOP | GG::INTERACTIVE | GG::DRAGABLE | GG::RESIZABLE | PINABLE, DES_PEDIA_WND_NAME);
    m_main_panel = GG::Wnd::Create<MainPanel>(DES_MAIN_WND_NAME);
    m_policy_palette = GG::Wnd::Create<PolicyPalette>(DES_PART_PALETTE_WND_NAME);
    m_base_selector = GG::Wnd::Create<BaseSelector>(DES_BASE_SELECTOR_WND_NAME);
    InitializeWindows();
    HumanClientApp::GetApp()->RepositionWindowsSignal.connect(
        boost::bind(&DesignWnd::InitializeWindows, this));

    AttachChild(m_detail_panel);

    AttachChild(m_main_panel);
    m_main_panel->PolicyTypeClickedSignal.connect(
        boost::bind(static_cast<void (EncyclopediaDetailPanel::*)(const Policy*)>(&EncyclopediaDetailPanel::SetItem), m_detail_panel, _1));
    m_main_panel->HullTypeClickedSignal.connect(
        boost::bind(static_cast<void (EncyclopediaDetailPanel::*)(const HullType*)>(&EncyclopediaDetailPanel::SetItem), m_detail_panel, _1));
    m_main_panel->DesignChangedSignal.connect(
        boost::bind(&DesignWnd::DesignChanged, this));
    m_main_panel->DesignNameChangedSignal.connect(
        boost::bind(&DesignWnd::DesignNameChanged, this));
    m_main_panel->CompleteDesignClickedSignal.connect(
        boost::bind(static_cast<void (EncyclopediaDetailPanel::*)(int)>(&EncyclopediaDetailPanel::SetDesign), m_detail_panel, _1));
    m_main_panel->Sanitize();

    AttachChild(m_policy_palette);
    m_policy_palette->PolicyTypeClickedSignal.connect(
        boost::bind(static_cast<void (EncyclopediaDetailPanel::*)(const Policy*)>(&EncyclopediaDetailPanel::SetItem), m_detail_panel, _1));
    m_policy_palette->PolicyTypeDoubleClickedSignal.connect(
        boost::bind(&DesignWnd::MainPanel::AddPolicy, m_main_panel, _1));
    m_policy_palette->ClearPolicySignal.connect(
        boost::bind(&DesignWnd::MainPanel::ClearPolicy, m_main_panel, _1));

    AttachChild(m_base_selector);

    m_base_selector->DesignSelectedSignal.connect(
        boost::bind(static_cast<void (MainPanel::*)(int)>(&MainPanel::SetDesign), m_main_panel, _1));
    m_base_selector->DesignComponentsSelectedSignal.connect(
        boost::bind(&MainPanel::SetDesignComponents, m_main_panel, _1, _2));
    m_base_selector->SavedDesignSelectedSignal.connect(
        boost::bind(static_cast<void (MainPanel::*)(const boost::uuids::uuid&)>(&MainPanel::SetDesign), m_main_panel, _1));

    m_base_selector->DesignClickedSignal.connect(
        boost::bind(static_cast<void (EncyclopediaDetailPanel::*)(const ShipDesign*)>(&EncyclopediaDetailPanel::SetItem), m_detail_panel, _1));
    m_base_selector->HullClickedSignal.connect(
        boost::bind(static_cast<void (EncyclopediaDetailPanel::*)(const HullType*)>(&EncyclopediaDetailPanel::SetItem), m_detail_panel, _1));
    m_base_selector->TabChangedSignal.connect(boost::bind(&MainPanel::HandleBaseTypeChange, m_main_panel, _1));

    // Connect signals to re-populate when policy obsolescence changes
    m_policy_palette->PolicyObsolescenceChangedSignal.connect(
        boost::bind(&BaseSelector::Reset, m_base_selector));
}

void DesignWnd::SizeMove(const GG::Pt& ul, const GG::Pt& lr) {
    const GG::Pt old_size = Size();
    GG::Wnd::SizeMove(ul, lr);
    if (old_size != Size()) {
        m_detail_panel->ValidatePosition();
        m_base_selector->ValidatePosition();
        m_policy_palette->ValidatePosition();
        m_main_panel->ValidatePosition();
    }
}

void DesignWnd::Reset() {
    m_policy_palette->Populate();
    m_base_selector->Reset();
    m_detail_panel->Refresh();
    m_main_panel->Sanitize();
}

void DesignWnd::Sanitize()
{ m_main_panel->Sanitize(); }

void DesignWnd::Render()
{ GG::FlatRectangle(UpperLeft(), LowerRight(), ClientUI::WndColor(), GG::CLR_ZERO, 0); }

void DesignWnd::InitializeWindows() {
    const GG::X selector_width = GG::X(250);
    const GG::X main_width = ClientWidth() - selector_width;

    const GG::Pt pedia_ul(selector_width, GG::Y0);
    const GG::Pt pedia_wh(5*main_width/11, 2*ClientHeight()/5);

    const GG::Pt main_ul(selector_width, pedia_ul.y + pedia_wh.y);
    const GG::Pt main_wh(main_width, ClientHeight() - main_ul.y);

    const GG::Pt palette_ul(selector_width + pedia_wh.x, pedia_ul.y);
    const GG::Pt palette_wh(main_width - pedia_wh.x, pedia_wh.y);

    const GG::Pt selector_ul(GG::X0, GG::Y0);
    const GG::Pt selector_wh(selector_width, ClientHeight());

    m_detail_panel-> InitSizeMove(pedia_ul,     pedia_ul + pedia_wh);
    m_main_panel->   InitSizeMove(main_ul,      main_ul + main_wh);
    m_policy_palette-> InitSizeMove(palette_ul,   palette_ul + palette_wh);
    m_base_selector->InitSizeMove(selector_ul,  selector_ul + selector_wh);
}

void DesignWnd::ShowPolicyTypeInEncyclopedia(const std::string& policy_type)
{ m_detail_panel->SetPolicyType(policy_type); }

void DesignWnd::ShowHullTypeInEncyclopedia(const std::string& hull_type)
{ m_detail_panel->SetHullType(hull_type); }

void DesignWnd::ShowShipDesignInEncyclopedia(int design_id)
{ m_detail_panel->SetDesign(design_id); }

void DesignWnd::DesignChanged() {
    m_detail_panel->SetIncompleteDesign(m_main_panel->GetIncompleteDesign());
    m_base_selector->Reset();
}

void DesignWnd::DesignNameChanged() {
    m_detail_panel->SetIncompleteDesign(m_main_panel->GetIncompleteDesign());
    m_base_selector->Reset();
}

void DesignWnd::EnableOrderIssuing(bool enable/* = true*/)
{ m_base_selector->EnableOrderIssuing(enable); }
