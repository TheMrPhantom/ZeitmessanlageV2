import React from 'react'
import { Route, Routes } from 'react-router-dom';

import Login from '../Common/Login/Login';
import OrganizationRun from '../Organization/Run/Run';
import Turnament from '../Organization/Turnament/Turnament';
import Participants from '../Organization/Participants/Participants';
import Dashboard from '../Organization/OrganizationDashboard/Dashboard';
import UserRun from '../User/Run/Run';
import RunSelection from '../User/RunSelection/RunSelection';
import PrintingPage from '../Organization/Printing/PrintingPage';
import Printing from '../Organization/Printing/Printing';
import { Toolbar } from '@mui/material';
type Props = {}

const Routing = (props: Props) => {
    return (
        <>
            <Routes>
                <Route path="/login" element={<><Toolbar /><Login /></>} />
                <Route path="/o/:organization" element={<><Toolbar /><Dashboard /></>} />
                <Route path="/o/:organization/:date" element={<><Toolbar /><Turnament /></>} />
                <Route path="/o/:organization/:date/participants" element={<><Toolbar /><Participants /></>} />
                <Route path="/o/:organization/:date/:class/:size" element={<><Toolbar /><OrganizationRun /></>} />
                <Route path="/o/:organization/:date/print" element={<Printing />} />
                <Route path="/u/:organization/:date" element={<><Toolbar /><RunSelection /></>} />
                <Route path="/u/:organization/:date/:class/:size" element={<><Toolbar /><UserRun /></>} />

            </Routes>
        </>
    )
}

export default Routing