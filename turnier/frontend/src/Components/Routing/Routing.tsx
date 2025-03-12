import React, { useState } from 'react'
import { Route, Routes } from 'react-router-dom';

import Login from '../Common/Login/Login';
import OrganizationRun from '../Organization/Run/Run';
import Turnament from '../Organization/Turnament/Turnament';
import Participants from '../Organization/Participants/Participants';
import Dashboard from '../Organization/OrganizationDashboard/Dashboard';
import UserRun from '../User/Run/Run';
import RunSelection from '../User/RunSelection/RunSelection';
import Printing from '../Organization/Printing/Printing';
import { Toolbar } from '@mui/material';
import { startSerial } from '../Common/StaticFunctionsTyped';
import UseOffline from '../Organization/UseOffline/UseOffline';
import PrintQR from '../Organization/Printing/PrintQR';
import Admin from '../Admin/Dashboard/Admin';
import SwhvExport from '../Organization/Printing/SwhvExport';
type Props = {}

const Routing = (props: Props) => {
    const [lastMessage, setlastMessage] = useState<string | null>(null)
    const [timeError, settimeError] = useState(true)
    const [connected, setconnected] = useState(false)
    const [timeMeasurementActive, settimeMeasurementActive] = useState(false)

    return (
        <>
            <Routes>
                <Route path="/login" element={<><Toolbar /><Login /></>} />
                <Route path="/use-offline" element={<><Toolbar />< UseOffline /></>} />

                {/*Organization pages*/}
                <Route path="/o/:organization" element={<><Toolbar /><Dashboard /></>} />
                <Route path="/o/:organization/:date" element={<><Toolbar /><Turnament /></>} />
                <Route path="/o/:organization/:date/participants" element={<><Toolbar /><Participants /></>} />
                <Route path="/o/:organization/:date/:class/:size" element={<><Toolbar /><OrganizationRun startSerial={() => {
                    startSerial((message) => {
                        setlastMessage(message)
                        /* Incoming message from Serial */
                        settimeError(false)
                        setconnected(true)
                    }, () => {
                        /* Serial connected */
                        console.log("Connected")
                        settimeError(false)
                        setconnected(true)
                    }, () => {
                        /* Serial disconnected */
                        settimeError(true)
                        setconnected(false)
                    })
                }} lastMessage={lastMessage}
                    timeError={timeError}
                    connected={connected}
                    setconnected={setconnected}
                    setlastMessage={setlastMessage}
                    timeMeasurementActive={timeMeasurementActive}
                    settimeMeasurementActive={settimeMeasurementActive}

                /></>} />
                <Route path="/o/:organization/:date/print" element={<Printing />} />
                <Route path="/o/:organization/:date/print/qr" element={<PrintQR />} />
                <Route path="/o/:organization/:date/export/swhv" element={<><Toolbar /><SwhvExport /></>} />

                {/*User pages*/}
                <Route path="/u/:organization/:date/:secret" element={<><Toolbar /><RunSelection /></>} />
                <Route path="/u/:organization/:date/:secret/:class/:size" element={<><Toolbar /><UserRun /></>} />
                <Route path="/u/:organization/:date/:secret/:class/:size/kombi" element={<><Toolbar /><UserRun /></>} />

                {/*Admin pages*/}
                <Route path="/admin" element={<><Toolbar /><Admin /></>} />
            </Routes>
        </>
    )
}

export default Routing