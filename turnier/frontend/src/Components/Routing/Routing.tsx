import React from 'react'
import { Route, Routes } from 'react-router-dom';

import Login from '../Common/Login/Login';
import OrganizationRun from '../Organization/Run/Run';
import Turnament from '../Organization/Turnament/Turnament';
import Participants from '../Organization/Participants/Participants';
import Dashboard from '../Organization/OrganizationDashboard/Dashboard';
import UserRun from '../User/Run/Run';
import RunSelection from '../User/RunSelection/RunSelection';
type Props = {}

const Routing = (props: Props) => {
    return (
        <>
            <Routes>
                <Route path="/login" element={<Login />} />
                <Route path="/o/:organization" element={<Dashboard />} />
                <Route path="/o/:organization/:date" element={<Turnament />} />
                <Route path="/o/:organization/:date/participants" element={<Participants />} />
                <Route path="/o/:organization/:date/:class/:size" element={<OrganizationRun />} />
                <Route path="/u/:organization/:date" element={<RunSelection />} />
                <Route path="/u/:organization/:date/:class/:size" element={<UserRun />} />

            </Routes>
        </>
    )
}

export default Routing