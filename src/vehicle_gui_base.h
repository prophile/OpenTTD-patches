/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file vehicle_gui_base.h Functions/classes shared between the different vehicle list GUIs. */

#ifndef VEHICLE_GUI_BASE_H
#define VEHICLE_GUI_BASE_H

#include "date_type.h"
#include "economy_type.h"
#include "sortlist_type.h"
#include "vehicle_base.h"
#include "vehiclelist.h"
#include "window_gui.h"
#include "widgets/dropdown_type.h"
#include "cargo_type.h"
#include <iterator>
#include <numeric>

typedef GUIList<const Vehicle*, std::nullptr_t, CargoID> GUIVehicleList;

struct GUIVehicleGroup {
	VehicleList::const_iterator vehicles_begin;    ///< Pointer to beginning element of this vehicle group.
	VehicleList::const_iterator vehicles_end;      ///< Pointer to past-the-end element of this vehicle group.

	GUIVehicleGroup(VehicleList::const_iterator vehicles_begin, VehicleList::const_iterator vehicles_end)
		: vehicles_begin(vehicles_begin), vehicles_end(vehicles_end) {}

	std::ptrdiff_t NumVehicles() const
	{
		return std::distance(this->vehicles_begin, this->vehicles_end);
	}

	const Vehicle *GetSingleVehicle() const
	{
		dbg_assert(this->NumVehicles() == 1);
		return this->vehicles_begin[0];
	}

	Money GetDisplayProfitThisYear() const
	{
		return std::accumulate(this->vehicles_begin, this->vehicles_end, (Money)0, [](Money acc, const Vehicle *v) {
			return acc + v->GetDisplayProfitThisYear();
		});
	}

	Money GetDisplayProfitLastYear() const
	{
		return std::accumulate(this->vehicles_begin, this->vehicles_end, (Money)0, [](Money acc, const Vehicle *v) {
			return acc + v->GetDisplayProfitLastYear();
		});
	}

	DateDelta GetOldestVehicleAge() const
	{
		const Vehicle *oldest = *std::max_element(this->vehicles_begin, this->vehicles_end, [](const Vehicle *v_a, const Vehicle *v_b) {
			return v_a->economy_age < v_b->economy_age;
		});
		return oldest->economy_age;
	}
};

typedef GUIList<GUIVehicleGroup, std::nullptr_t, CargoID> GUIVehicleGroupList;

struct BaseVehicleListWindow : public Window {
	enum GroupBy : uint8_t {
		GB_NONE,
		GB_SHARED_ORDERS,

		GB_END,
	};

	GroupBy grouping;                         ///< How we want to group the list.
protected:
	VehicleList vehicles;                     ///< List of vehicles.  This is the buffer for `vehgroups` to point into; if this is structurally modified, `vehgroups` must be rebuilt.
public:
	uint own_vehicles = 0;                    ///< Count of vehicles of the local company
	CompanyID own_company;                    ///< Company ID used for own_vehicles
	GUIVehicleGroupList vehgroups;            ///< List of (groups of) vehicles.  This stores iterators of `vehicles`, and should be rebuilt if `vehicles` is structurally changed.
	Listing *sorting;                         ///< Pointer to the vehicle type related sorting.
	uint8_t unitnumber_digits;                ///< The number of digits of the highest unit number.
	Scrollbar *vscroll;
	VehicleListIdentifier vli;                  ///< Identifier of the vehicle list we want to currently show.
	VehicleID vehicle_sel;                      ///< Selected vehicle
	CargoID cargo_filter_criteria;              ///< Selected cargo filter index
	uint order_arrow_width;                     ///< Width of the arrow in the small order list.
	CargoTypes used_cargoes;

	typedef GUIVehicleGroupList::SortFunction VehicleGroupSortFunction;
	typedef GUIVehicleList::SortFunction VehicleIndividualSortFunction;

	inline CargoID GetCargoFilter() const { return this->cargo_filter_criteria; }

	enum ActionDropdownItem {
		ADI_TEMPLATE_REPLACE,
		ADI_REPLACE,
		ADI_SERVICE,
		ADI_DEPOT,
		ADI_DEPOT_SELL,
		ADI_CANCEL_DEPOT,
		ADI_ADD_SHARED,
		ADI_REMOVE_ALL,
		ADI_CHANGE_ORDER,
		ADI_CREATE_GROUP,
		ADI_TRACERESTRICT_SLOT_MGMT,
		ADI_TRACERESTRICT_COUNTER_MGMT,
	};

	static const StringID vehicle_depot_name[];
	static const StringID vehicle_depot_sell_name[];

	static const StringID vehicle_group_by_names[];
	static const StringID vehicle_group_none_sorter_names[];
	static const StringID vehicle_group_shared_orders_sorter_names[];
	static VehicleGroupSortFunction * const vehicle_group_none_sorter_funcs[];
	static VehicleGroupSortFunction * const vehicle_group_shared_orders_sorter_funcs[];

	BaseVehicleListWindow(WindowDesc *desc, WindowNumber wno);

	void OnInit() override;

	void UpdateSortingInterval();
	void UpdateSortingFromGrouping();

	void DrawVehicleListItems(VehicleID selected_vehicle, int line_height, const Rect &r) const;
	void UpdateVehicleGroupBy(GroupBy group_by);
	void SortVehicleList();
	void CountOwnVehicles();
	void BuildVehicleList();
	void SetCargoFilter(uint8_t index);
	void SetCargoFilterArray();
	void FilterVehicleList();
	StringID GetCargoFilterLabel(CargoID cid) const;
	DropDownList BuildCargoDropDownList(bool full) const;
	Dimension GetActionDropdownSize(bool show_autoreplace, bool show_group, bool show_template_replace, StringID change_order_str = 0);
	DropDownList BuildActionDropdownList(bool show_autoreplace, bool show_group, bool show_template_replace,
			StringID change_order_str = 0, bool show_create_group = false, bool consider_top_level = false);
	bool ShouldShowActionDropdownList() const;

	const StringID *GetVehicleSorterNames()
	{
		switch (this->grouping) {
			case GB_NONE:
				return vehicle_group_none_sorter_names;
			case GB_SHARED_ORDERS:
				return vehicle_group_shared_orders_sorter_names;
			default:
				NOT_REACHED();
		}
	}

	VehicleGroupSortFunction * const *GetVehicleSorterFuncs()
	{
		switch (this->grouping) {
			case GB_NONE:
				return vehicle_group_none_sorter_funcs;
			case GB_SHARED_ORDERS:
				return vehicle_group_shared_orders_sorter_funcs;
			default:
				NOT_REACHED();
		}
	}

	uint GetSorterDisableMask(VehicleType type) const;
};

uint GetVehicleListHeight(VehicleType type, uint divisor = 1);

struct Sorting {
	Listing aircraft;
	Listing roadveh;
	Listing ship;
	Listing train;
};

extern BaseVehicleListWindow::GroupBy _grouping[VLT_END][VEH_COMPANY_END];
extern Sorting _sorting[BaseVehicleListWindow::GB_END];

#endif /* VEHICLE_GUI_BASE_H */
