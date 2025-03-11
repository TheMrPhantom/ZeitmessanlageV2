import { Person, SettingsOutlined } from '@mui/icons-material'
import { AppBar, Avatar, Badge, Button, IconButton, Stack, Toolbar, Typography } from '@mui/material'
import React, { useEffect, useRef, useState } from 'react'
import Spacer from '../Spacer'
import { useLocation, useNavigate } from 'react-router-dom';
import { useDispatch, useSelector } from 'react-redux';
import { CommonReducerType } from '../../../Reducer/CommonReducer';
import { doPostRequest, loadPermanent, updateDatabase, wait } from '../StaticFunctionsTyped';
import Divider from '@mui/material/Divider';
import ListItem from '@mui/material/ListItem';
import ListItemButton from '@mui/material/ListItemButton';
import ListItemIcon from '@mui/material/ListItemIcon';
import ListItemText from '@mui/material/ListItemText';
import Box from '@mui/material/Box';
import List from '@mui/material/List';
import SportsBarIcon from '@mui/icons-material/SportsBar';
import PersonIcon from '@mui/icons-material/Person';
import ReceiptLongIcon from '@mui/icons-material/ReceiptLong';
import { Settings } from '@mui/icons-material';
import MenuIcon from '@mui/icons-material/Menu';
import AccountBalanceIcon from '@mui/icons-material/AccountBalance';
import { RootState } from '../../../Reducer/reducerCombiner';
import InfoIcon from '@mui/icons-material/Info';
import RequestQuoteIcon from '@mui/icons-material/RequestQuote';
import { ABRECHNUNGEN, EINSTELLUNGEN, GELD_ANFORDERN, GETRAENKE, MITGLIEDER, NUTZER_DASHBOARD, TRANSAKTIONEN, UEBERWEISEN } from '../Internationalization/i18n';
import Cookies from 'js-cookie';
import { setLoginState, setRequestDialogOpen, setTransferDialogOpen } from '../../../Actions/CommonAction';
import { classToString, sizeToString } from '../StaticFunctionsTyped';
import About from './About';
import WifiIcon from '@mui/icons-material/Wifi';
import WarningIcon from '@mui/icons-material/Warning';
import CheckCircleIcon from '@mui/icons-material/CheckCircle';
import { setSendingFailed } from '../../../Actions/SampleAction';
import CloudSyncIcon from '@mui/icons-material/CloudSync';
import ArrowBackIcon from '@mui/icons-material/ArrowBack';

type Props = {}


const TopBar = (props: Props) => {
    const navigate = useNavigate();
    const location = useLocation();
    // eslint-disable-next-line
    const drawerWidth = 240;
    const common: CommonReducerType = useSelector((state: RootState) => state.common);
    const dispatch = useDispatch()
    const [drawerOpen, setdrawerOpen] = useState(true)
    // eslint-disable-next-line
    const [drawerVisible, setdrawerVisible] = useState(true)
    const [aboutDialogOpen, setaboutDialogOpen] = useState(false)

    const showDrawerButton = () => {
        if (location.pathname.startsWith("/admin")) {
            return true
        } else if (location.pathname.startsWith("/user")) {
            const memberIDCookie = Cookies.get(window.globalTS.AUTH_COOKIE_PREFIX + "memberID")
            const memberIDCookieSafe = memberIDCookie ? parseInt(memberIDCookie) : -1
            const isAdmin = memberIDCookieSafe === 1
            const isCorrectUser = location.pathname.endsWith(memberIDCookieSafe.toString())
            if (isAdmin || isCorrectUser) {
                return true
            }
        }
        return false
    }

    useEffect(() => {
        if (location.pathname.startsWith("/admin") && window.innerWidth > window.globalTS.MOBILE_THRESHOLD) {
            setdrawerOpen(true)
            setdrawerVisible(true)
        } else {
            setdrawerOpen(false)
            setdrawerVisible(false)
        }
    }, [location.pathname])

    const lock = useRef(false);

    useEffect(() => {
        if (common.sendingFailed) {
            if (lock.current) {
                return;
            }

            lock.current = true;
            const trySend = async () => {
                while (common.sendingFailed) {
                    const localValidationData = localStorage.getItem("validation");
                    if (localValidationData) {
                        const organization = JSON.parse(localValidationData).name;
                        const org = loadPermanent(organization, dispatch, common, true);
                        if (org) {
                            const updatePromises = org.turnaments.map(turnament =>
                                updateDatabase(turnament, organization, dispatch)
                            );
                            const results = await Promise.all(updatePromises);
                            const success = results.every(result => result);

                            if (success) {
                                dispatch(setSendingFailed(false));

                            }
                        }
                    }
                    await wait(5000);
                }
                lock.current = false;
            }

            trySend();
        }

    }, [common.sendingFailed, dispatch, common]);

    const navigationButton = () => {
        if (location.pathname.startsWith("/admin")) {
            return <IconButton sx={{ flexGrow: 1 }} color="inherit" onClick={() => navigate("/")}><Person /></IconButton>
        } else {
            return <IconButton sx={{ flexGrow: 1 }} color="inherit" onClick={() => navigate("admin")}><SettingsOutlined /></IconButton>
        }
    }

    const hideDrawer = () => {
        setTimeout(() => { setdrawerVisible(false) }, 250)
    }

    // eslint-disable-next-line
    const getIcon = () => {
        if (showDrawerButton()) {
            return <IconButton
                color="inherit"
                aria-label="open drawer"
                onClick={() => {
                    if (drawerOpen) {
                        setdrawerOpen(false)
                        hideDrawer()
                    }
                    else {
                        setdrawerOpen(true)
                        setdrawerVisible(true)
                    }
                }}
                edge="start"
                sx={{
                    marginRight: 5
                }}
            >
                <MenuIcon />
            </IconButton>
        } else {
            return <></>
        }
    }

    const shouldDisplayAbout = () => {
        return window.globalTS.ORGANISATION_NAME !== "" ||
            window.globalTS.ABOUT_LINK !== "" ||
            window.globalTS.PRIVACY_LINK !== "" ||
            window.globalTS.ADDITIONAL_INFORMATION !== ""
    }

    const isUser = () => {
        return parseInt(Cookies.get(window.globalTS.AUTH_COOKIE_PREFIX + "memberID") as string)
    }

    const adminDrawer = () => {
        return <Box sx={{ overflow: 'auto' }}>
            <List>

                <ListItem disablePadding>
                    <ListItemButton onClick={() => navigate("/")}>
                        <ListItemIcon>
                            <Person />
                        </ListItemIcon>
                        <ListItemText primary={NUTZER_DASHBOARD} />
                    </ListItemButton>
                </ListItem>

            </List>
            <Divider />
            <List>
                <ListItem disablePadding>
                    <ListItemButton onClick={() => navigate("/admin/drinks")}>
                        <ListItemIcon>
                            <SportsBarIcon />
                        </ListItemIcon>
                        <ListItemText primary={GETRAENKE} />
                    </ListItemButton>
                </ListItem>
                <ListItem disablePadding>
                    <ListItemButton onClick={() => navigate("/admin/members")}>
                        <ListItemIcon>
                            <PersonIcon />
                        </ListItemIcon>
                        <ListItemText primary={MITGLIEDER} />
                    </ListItemButton>
                </ListItem>
                <ListItem disablePadding>
                    <ListItemButton onClick={() => navigate("/admin/transactions")}>
                        <ListItemIcon>
                            <ReceiptLongIcon />
                        </ListItemIcon>
                        <ListItemText primary={TRANSAKTIONEN} />
                    </ListItemButton>
                </ListItem>
                <ListItem disablePadding>
                    <ListItemButton onClick={() => navigate("/admin/checkout")}>
                        <ListItemIcon>
                            <AccountBalanceIcon />
                        </ListItemIcon>
                        <ListItemText primary={ABRECHNUNGEN} />
                    </ListItemButton>
                </ListItem>
                <ListItem disablePadding>
                    <ListItemButton onClick={() => navigate("/admin/settings")}>
                        <ListItemIcon>
                            <Settings />
                        </ListItemIcon>
                        <ListItemText primary={EINSTELLUNGEN} />
                    </ListItemButton>
                </ListItem>

            </List>
        </Box>
    }

    const userDrawer = () => {
        return <Box sx={{ overflow: 'auto' }}>
            <List>
                <ListItem disablePadding>
                    <ListItemButton onClick={() => {
                        setdrawerOpen(false)
                        hideDrawer()
                        dispatch(setTransferDialogOpen(true))
                    }}>
                        <ListItemIcon>
                            <AccountBalanceIcon />
                        </ListItemIcon>
                        <ListItemText primary={UEBERWEISEN} />
                    </ListItemButton>
                </ListItem>
                <ListItem disablePadding>
                    <ListItemButton onClick={() => {
                        setdrawerOpen(false)
                        hideDrawer()
                        dispatch(setRequestDialogOpen(true))
                    }}>
                        <ListItemIcon>
                            <RequestQuoteIcon />
                        </ListItemIcon>
                        <ListItemText primary={GELD_ANFORDERN} />
                    </ListItemButton>
                </ListItem>
            </List>
        </Box>
    }

    // eslint-disable-next-line
    const displayDrawer = () => {
        if (location.pathname.startsWith("/admin")) {
            return adminDrawer()
        } else if (location.pathname.startsWith("/user/")) {
            return userDrawer()
        }
        return <></>
    }

    const runRegex = new RegExp("/o/.*/\\d{4}-\\d{2}-\\d{2}/\\d/\\d");

    if (location.pathname.includes("/print")) {
        return <></>
    }

    return (
        <>
            <AppBar position="fixed" sx={{ zIndex: (theme) => theme.zIndex.drawer + 1, maxHeight: "77px" }}>
                <Toolbar sx={{ justifyContent: "space-between" }}>
                    <div style={{ display: "flex" }}>
                        {/*getIcon() Disabled drawer*/}
                        <IconButton
                            color="inherit"
                            onClick={() => {
                                navigate(-1)
                            }}
                        >
                            <ArrowBackIcon />
                        </IconButton>
                        <Spacer horizontal={20} />
                        <Button
                            size="large"
                            color="inherit"
                            onClick={() =>
                                navigate(isUser() !== 1 && isUser() !== 2 ? "/o/" + common.organization.name : "/")
                            }
                            sx={{ display: "inline-flex", maxHeight: "77px" }}
                            variant="text">
                            <Stack direction="row" alignItems="center" gap={3}>
                                <img
                                    src={`/Logo-Simple.svg`}
                                    height={50}
                                    alt='Logo'
                                    loading="lazy"
                                />
                                {window.globalTS.HOME_BUTTON}
                            </Stack>
                        </Button>
                    </div>
                    {/*Show run and size for current run if on run page*/
                        runRegex.test(location.pathname) ?
                            <>
                                <Stack direction="row" gap={1}>
                                    <Typography variant='h5'>{classToString(Number(location.pathname.split("/")[4]))}</Typography>
                                    <Typography variant='h5'>-</Typography>
                                    <Typography variant='h5'>{sizeToString(Number(location.pathname.split("/")[5]))}</Typography>
                                </Stack>
                            </> : <></>
                    }
                    <div style={{ display: "flex", alignItems: "center" }}>
                        <IconButton style={{ color: "white" }} onClick={() => { navigate("/use-offline") }}>
                            <CloudSyncIcon />
                        </IconButton>
                        <Spacer horizontal={20} />
                        <Badge
                            overlap="circular"
                            anchorOrigin={{ vertical: 'bottom', horizontal: 'right' }}
                            badgeContent={
                                <Avatar sx={{ bgcolor: "#00000000", width: 20, height: 20 }}>
                                    {!common.sendingFailed ? <CheckCircleIcon color='success' sx={{ width: 15, height: 15 }} /> :
                                        <WarningIcon color='warning' sx={{ width: 15, height: 15 }} />}
                                </Avatar>
                            }
                        >
                            <WifiIcon />
                        </Badge>
                        <Spacer horizontal={20} />
                        {shouldDisplayAbout() ? <IconButton
                            color="inherit"
                            onClick={() => {
                                setaboutDialogOpen(true)
                            }}
                        >
                            <InfoIcon />
                        </IconButton> : <></>}
                        <About isOpen={aboutDialogOpen} close={() => setaboutDialogOpen(false)} />
                        {navigationButton()}
                        <Spacer horizontal={20} />
                        <Button color="inherit" onClick={() => {
                            if (common.isLoggedIn) {
                                doPostRequest("logout", "", dispatch)
                                dispatch(setLoginState(null))
                            }
                            navigate("/login")
                        }}>{common.isLoggedIn !== null ? "Logout" : "Login"}</Button>
                    </div>
                </Toolbar>
            </AppBar>
            {/*
            <Slide direction="right" in={drawerOpen}>
                <Drawer
                    variant="permanent"
                    sx={{
                        display: drawerVisible ? "" : "none",
                        width: drawerWidth,
                        flexShrink: 0,
                        [`& .MuiDrawer-paper`]: { width: drawerWidth, boxSizing: 'border-box' },
                    }}
                >
                    <Toolbar />
                    {displayDrawer()}
                </Drawer>
            </Slide>
            */}

        </>
    )
}

export default TopBar