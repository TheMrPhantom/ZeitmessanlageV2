import React from 'react'
import { Route, Routes } from 'react-router-dom';
import Checkout from '../Admin/Checkout/Checkout';
import Drinks from '../Admin/Drinks/Drinks';
import Members from '../Admin/Members/Members';
import AdminOverview from '../Admin/Overview/Overview';
import Settings from '../Admin/Settings/Settings';
import Transactions from '../Admin/Transactions/Transactions';
import Login from '../Common/Login/Login';
import Details from '../User/Details/Details';
import UserOverview from '../User/Overview/Overview';
import Message from '../Common/Message/Message';
import MainConfigurator from '../Admin/Configurator/MainConfigurator';
import MainConfiguratorInit from '../Admin/Configurator/MainConfiguratorInit';

type Props = {}

const Routing = (props: Props) => {
    return (
        <>
            <Routes>
                <Route path="/" element={<UserOverview />} />
                <Route path="/login" element={<Login />} />

            </Routes>
        </>
    )
}

export default Routing