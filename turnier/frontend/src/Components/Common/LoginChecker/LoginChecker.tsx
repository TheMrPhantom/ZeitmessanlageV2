import Cookies from 'js-cookie';
import React, { useEffect } from 'react'
import { useDispatch } from 'react-redux';
import { useLocation, useNavigate } from 'react-router-dom';
import { setLoginState } from '../../../Actions/CommonAction';
import { doGetRequest } from '../StaticFunctions';

type Props = {}

const LoginChecker = (props: Props) => {
    const location = useLocation();
    const navigate = useNavigate();
    const dispatch = useDispatch();

    useEffect(() => {
        let requestString = ""
        if (location.pathname.startsWith("/admin")) {
            requestString = "login/admin/check"
        } else if (location.pathname.startsWith("/message")) {
            return
        } else if (location.pathname.startsWith("/config")) {
            return
        } else if (!(location.pathname.startsWith("/login") || location.pathname.startsWith("/u"))) {
            requestString = "login/check"
        } else {
            return
        }
        console.log(Cookies.get(window.globalTS.AUTH_COOKIE_PREFIX + "memberID"))
        doGetRequest(requestString).then((value) => {
            if (value.code !== 200) {
                // User needs to login

                navigate("/login?originalPath=" + location.pathname)
                dispatch(setLoginState(false))

            } else {
                // User is logged login
                dispatch(setLoginState(true))

                const memberID = Cookies.get(window.globalTS.AUTH_COOKIE_PREFIX + "memberID");
                const notUndefined = memberID !== undefined ? parseInt(memberID) : 0;

                if (notUndefined > 2) {
                    navigate("/user/" + Cookies.get(window.globalTS.AUTH_COOKIE_PREFIX + "memberID"))
                }
            }
        })

    }, [location.pathname, navigate, dispatch])

    return (
        <></>
    )
}

export default LoginChecker