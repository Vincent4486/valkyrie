	# SPDX-License-Identifier: GPL-3.0-only

.extern i686_Scheduler_ContextSwitch_Impl

.global i686_Scheduler_ContextSwitch
i686_Scheduler_ContextSwitch:
	call i686_Scheduler_ContextSwitch_Impl
	ret