import Cookies from 'js-cookie';
import React, { useCallback, useEffect } from 'react'
import { useDispatch } from 'react-redux';
import { useLocation, useNavigate } from 'react-router-dom';
import { openToast, setLoginState } from '../../../Actions/CommonAction';
import { doGetRequest } from '../StaticFunctionsTyped';
import { verifySignature } from '../StaticFunctionsTyped';
import { Verification } from '../../../types/ResponseTypes';

type Props = {}

const LoginChecker = (props: Props) => {
    const location = useLocation();
    const navigate = useNavigate();
    const dispatch = useDispatch();

    const loggedIn = useCallback((value: {
        code: number;
        content: any;
    } | {
        code: number;
        content?: undefined;
    }) => {
        dispatch(setLoginState(value.content))
        if (!window.location.pathname.startsWith(`/o/${value.content.name}`)) {
            navigate("/o/" + value.content.name)
        }
    }, [dispatch, navigate])

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
        doGetRequest(requestString, dispatch).then((value) => {
            if (value.code !== 200 && value.code !== 503) {
                // User needs to login
                navigate("/login?originalPath=" + location.pathname)
                dispatch(setLoginState(null))

            } else if (value.code === 503) {
                const verificationString: string | null = window.localStorage.getItem("validation")
                const verification: Verification = verificationString !== null ? JSON.parse(verificationString) : null

                if (verification !== null) {
                    if (verifySignature(verification.validUntil + verification.name, verification.signedValidation)) {
                        if (new Date(verification.validUntil) < new Date()) {
                            navigate("/login?originalPath=" + location.pathname)
                            dispatch(setLoginState(null))
                            dispatch(openToast({ message: "Lizenz ist abgelaufen, bitte verlängern", type: "error", headline: "Lizenz abgelaufen", duration: 10000 }))
                        } else {
                            dispatch(setLoginState({ name: verification.name, alias: verification.alias }))
                            if (!window.location.pathname.startsWith(`/o/${verification.name}`)) {
                                navigate("/o/" + verification.name)
                            }
                        }
                    } else {
                        dispatch(openToast({ message: "Die Verifikationdatei wurde manipuliert, bitte versuche es später erneut mit Internetverbindung", type: "error", headline: "Server nicht erreichbar", duration: 10000 }))
                    }
                } else {
                    dispatch(openToast({ message: "Kein Account lokal hinterlegt, probiere es später erneut mit Internetverbindung", type: "error", headline: "Server nicht erreichbar", duration: 10000 }))
                }
            } else {

                // User is logged login
                loggedIn(value)
            }


        })

    }, [location.pathname, navigate, dispatch, loggedIn])

    return (
        <></>
    )
}

export default LoginChecker